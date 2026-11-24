/*
 * hxfs_tc — modular IAssetPlugin for True Combat: Elite (ET 2.60b).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "helix/fs/iasset_plugin.h"
#include "archive/archive.h"
#include "bsp/names.h"
#include "bsp/read.h"
#include "bsp/helix.h"
#include "cache.h"
#include "emit.h"
#include "tc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

#ifdef _WIN32
#include <windows.h>
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
#else
#include <dirent.h>
#endif

typedef struct TcPlugin {
    hxAssetPlugin api;
    TcArchive* archive;
} TcPlugin;

static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static int read_disk_file(const char* path, unsigned char** out, unsigned int* out_size) {
    FILE* f;
    long sz;
    unsigned char* data;
    if (!path || !out || !out_size) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    data = (unsigned char*)malloc((size_t)sz ? (size_t)sz : 1);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (sz && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = data;
    *out_size = (unsigned int)sz;
    return 1;
}

static int add_path(TcPlugin* p, const char* path) {
    size_t len;
    if (!p || !p->archive || !path || !path[0]) return 0;
    len = strlen(path);
    if (len >= 4 && tc_strcasecmp(path + len - 4, ".bsp") == 0) {
        unsigned char* data = NULL;
        unsigned int sz = 0;
        const char* base;
        char vpath[256];
        int ok;
        if (!read_disk_file(path, &data, &sz) || !bsp_read_valid(data, sz)) {
            free(data);
            return 0;
        }
        base = strrchr(path, '/');
        if (!base) base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        snprintf(vpath, sizeof(vpath), "maps/%s", base);
        ok = tc_archive_add_blob(p->archive, vpath, data, sz);
        free(data);
        return ok;
    }
    return tc_archive_add_zip(p->archive, path);
}

static int tc_open(hxAssetPlugin* self, const char* path) {
    TcPlugin* p = (TcPlugin*)self;
    if (!p || !p->archive) return 0;
    tc_archive_clear(p->archive);
    return add_path(p, path);
}

static int tc_open_many(hxAssetPlugin* self, const char** paths, int count) {
    TcPlugin* p = (TcPlugin*)self;
    int i, opened = 0;
    if (!p || !p->archive) return 0;
    tc_archive_clear(p->archive);
    if (!paths) return 0;
    for (i = 0; i < count; i++) {
        if (add_path(p, paths[i])) opened++;
    }
    return opened;
}

static int tc_open_dir(hxAssetPlugin* self, const char* dir_path) {
    TcPlugin* p = (TcPlugin*)self;
    char** paths = NULL;
    int pcount = 0, pcap = 0, i, opened = 0;
    if (!p || !p->archive) return 0;
    tc_archive_clear(p->archive);
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
                char* slot;
                char** grown;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (pcount == pcap) {
                    pcap = pcap ? pcap * 2 : 16;
                    grown = (char**)realloc(paths, (size_t)pcap * sizeof(char*));
                    if (!grown) break;
                    paths = grown;
                }
                slot = (char*)malloc(strlen(dir_path) + strlen(fd.cFileName) + 2);
                if (!slot) break;
                sprintf(slot, "%s/%s", dir_path, fd.cFileName);
                paths[pcount++] = slot;
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
                size_t nlen = strlen(ent->d_name);
                if (nlen >= 4 && tc_strcasecmp(ent->d_name + nlen - 4, ".pk3") == 0) {
                    char* slot;
                    char** grown;
                    if (pcount == pcap) {
                        pcap = pcap ? pcap * 2 : 16;
                        grown = (char**)realloc(paths, (size_t)pcap * sizeof(char*));
                        if (!grown) break;
                        paths = grown;
                    }
                    slot = (char*)malloc(strlen(dir_path) + nlen + 2);
                    if (!slot) break;
                    sprintf(slot, "%s/%s", dir_path, ent->d_name);
                    paths[pcount++] = slot;
                }
            }
            closedir(dir);
        }
    }
#endif

    if (pcount > 1) qsort(paths, (size_t)pcount, sizeof(char*), cmp_str);
    for (i = 0; i < pcount; i++) {
        if (add_path(p, paths[i])) opened++;
        free(paths[i]);
    }
    free(paths);
    return opened;
}

static int with_ext(const char* name, const char* ext, char* out, size_t cap) {
    const char* slash;
    const char* base;
    const char* dot;
    size_t stem, elen;
    if (!name || !ext || !out || cap == 0) return 0;
    slash = strrchr(name, '/');
    base = slash ? slash + 1 : name;
    dot = strrchr(base, '.');
    stem = dot ? (size_t)(dot - name) : strlen(name);
    elen = strlen(ext);
    if (stem + elen + 1 > cap) return 0;
    memcpy(out, name, stem);
    memcpy(out + stem, ext, elen + 1);
    return strcmp(out, name) != 0;
}

static int texture_alt_exists(const TcArchive* ar, const char* name) {
    char alt[512];
    if (with_ext(name, ".tga", alt, sizeof(alt)) && tc_archive_contains(ar, alt)) return 1;
    if (with_ext(name, ".jpg", alt, sizeof(alt)) && tc_archive_contains(ar, alt)) return 1;
    if (with_ext(name, ".dds", alt, sizeof(alt)) && tc_archive_contains(ar, alt)) return 1;
    return 0;
}

static int texture_source(const TcArchive* ar, const char* name, unsigned char** raw, unsigned int* rsz) {
    char alt[512];
    if (tc_archive_read(ar, name, raw, rsz)) return 1;
    if (with_ext(name, ".tga", alt, sizeof(alt)) && tc_archive_read(ar, alt, raw, rsz)) return 1;
    if (with_ext(name, ".jpg", alt, sizeof(alt)) && tc_archive_read(ar, alt, raw, rsz)) return 1;
    if (with_ext(name, ".dds", alt, sizeof(alt)) && tc_archive_read(ar, alt, raw, rsz)) return 1;
    return 0;
}

static int tc_contains(hxAssetPlugin* self, const char* name) {
    TcPlugin* p = (TcPlugin*)self;
    char candidates[3][256];
    char map_name[128];
    int n, i;

    if (!p || !p->archive || !name || !name[0]) return 0;
    if (tc_archive_contains(p->archive, name)) return 1;

    n = bsp_candidate_paths(name, candidates, 3, map_name, sizeof(map_name));
    for (i = 0; i < n; i++) {
        if (tc_archive_contains(p->archive, candidates[i])) return 1;
    }

    if (texture_alt_exists(p->archive, name)) return 1;
    return 0;
}

static void plugin_dir(char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
#ifdef _WIN32
    {
        HMODULE hm = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)plugin_dir, &hm)) {
            char path[MAX_PATH];
            DWORD len = GetModuleFileNameA(hm, path, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                char* slash = strrchr(path, '\\');
                char* fwd = strrchr(path, '/');
                if (fwd && (!slash || fwd > slash)) slash = fwd;
                if (slash) *slash = 0;
                snprintf(out, cap, "%s", path);
            }
        }
    }
#endif
    if (!out[0]) snprintf(out, cap, ".");
}

static int tc_read(hxAssetPlugin* self, const char* name,
                   unsigned char** out_buf, unsigned int* out_size) {
    TcPlugin* p = (TcPlugin*)self;
    char candidates[3][256];
    char map_name[128];
    char cache_dir[512];
    int n, i;

    if (!p || !p->archive || !name || !name[0] || !out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;

    /* Virtual .hxmap translation */
    n = bsp_candidate_paths(name, candidates, 3, map_name, sizeof(map_name));
    for (i = 0; i < n; i++) {
        unsigned char* bsp_data = NULL;
        unsigned int bsp_sz = 0;
        unsigned int crc = 0;
        if (!tc_archive_contains(p->archive, candidates[i])) continue;
        if (!tc_archive_checksum(p->archive, candidates[i], &crc)) continue;

        plugin_dir(cache_dir, sizeof(cache_dir));
        if (tc_cache_load(cache_dir, name, crc, out_buf, out_size)) {
            return 1;
        }

        if (tc_archive_read(p->archive, candidates[i], &bsp_data, &bsp_sz)) {
            int ok = bsp_to_helix_map(bsp_data, (size_t)bsp_sz, map_name, out_buf, out_size);
            free(bsp_data);
            if (ok) {
                tc_cache_store(cache_dir, name, crc, *out_buf, *out_size);
                return 1;
            }
        }
    }

    /* Direct archive read */
    if (tc_archive_read(p->archive, name, out_buf, out_size)) return 1;

    /* Texture fallback (.tga, .jpg, .dds) */
    if (texture_source(p->archive, name, out_buf, out_size)) return 1;

    return 0;
}

static void tc_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void tc_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    TcPlugin* p = (TcPlugin*)self;
    char** src = NULL;
    unsigned int scount = 0, i;
    char** out = NULL;
    unsigned int total = 0, cap = 0;

    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p || !p->archive) return;

    tc_archive_list(p->archive, &src, &scount);
    for (i = 0; i < scount; i++) {
        char alias[512];
        if (total == cap) {
            cap = cap ? cap * 2 : 32;
            out = (char**)realloc(out, (size_t)cap * sizeof(char*));
            if (!out) goto fail;
        }
        out[total++] = src[i];
        src[i] = NULL;

        if (bsp_hxmap_alias(out[total - 1], alias, sizeof(alias))) {
            if (total == cap) {
                cap = cap ? cap * 2 : 32;
                out = (char**)realloc(out, (size_t)cap * sizeof(char*));
                if (!out) goto fail;
            }
            out[total++] = tc_strdup(alias);
        }
    }
    free(src);

    if (total > 1) qsort(out, (size_t)total, sizeof(char*), cmp_str);
    *names = out;
    *count = total;
    return;

fail:
    if (src) {
        for (i = 0; i < scount; i++) free(src[i]);
        free(src);
    }
    if (out) {
        for (i = 0; i < total; i++) free(out[i]);
        free(out);
    }
}

static void tc_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    unsigned int i;
    (void)self;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static void tc_close(hxAssetPlugin* self) {
    TcPlugin* p = (TcPlugin*)self;
    if (!p || !p->archive) return;
    tc_archive_clear(p->archive);
}

static void tc_destroy(hxAssetPlugin* self) {
    TcPlugin* p = (TcPlugin*)self;
    if (!p) return;
    if (p->archive) tc_archive_destroy(p->archive);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    TcPlugin* p = (TcPlugin*)calloc(1, sizeof(TcPlugin));
    if (!p) return NULL;
    p->archive = tc_archive_create();
    if (!p->archive) {
        free(p);
        return NULL;
    }
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "tc";
    p->api.open        = tc_open;
    p->api.open_many   = tc_open_many;
    p->api.open_dir    = tc_open_dir;
    p->api.contains    = tc_contains;
    p->api.read        = tc_read;
    p->api.free_buf    = tc_free_buf;
    p->api.list        = tc_list;
    p->api.free_list   = tc_free_list;
    p->api.close       = tc_close;
    p->api.destroy     = tc_destroy;
    return &p->api;
}
