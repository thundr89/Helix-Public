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

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxAssetPlugin api;
    WadFile* wad;
} HxfsWad;

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsWad* p = (HxfsWad*)self;
    WadFile* next;
    if (!p) return 0;
    if (p->wad) {
        wad_close(p->wad);
        p->wad = NULL;
    }
    if (!path || !path[0]) return 0;
    next = wad_open_file(path);
    if (!next) return 0;
    p->wad = next;
    return 1;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p || !p->wad) return 0;
    return wad_contains(p->wad, name);
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p || !p->wad) return 0;
    return wad_read(p->wad, name, out_buf, out_size);
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsWad* p = (HxfsWad*)self;
    if (!names || !count) return;
    if (!p || !p->wad) {
        *names = NULL;
        *count = 0;
        return;
    }
    wad_list(p->wad, names, count);
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    wad_free_list(names, count);
}

static void hxfs_close(hxAssetPlugin* self) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p) return;
    if (p->wad) {
        wad_close(p->wad);
        p->wad = NULL;
    }
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p) return;
    hxfs_close(self);
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
    p->api.destroy = hxfs_destroy;
    return &p->api;
}
