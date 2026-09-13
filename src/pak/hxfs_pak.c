/*
 * hxfs_pak — Helix IAssetPlugin for Quake 1/2 PAK archives.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "pak_file.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxAssetPlugin api;
    PakFile* pak;
} HxfsPak;

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsPak* p = (HxfsPak*)self;
    PakFile* next;
    if (!p) return 0;
    if (p->pak) {
        pak_close(p->pak);
        p->pak = NULL;
    }
    if (!path || !path[0]) return 0;
    next = pak_open_file(path);
    if (!next) return 0;
    p->pak = next;
    return 1;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsPak* p = (HxfsPak*)self;
    if (!p || !p->pak) return 0;
    return pak_contains(p->pak, name);
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsPak* p = (HxfsPak*)self;
    if (!p || !p->pak) return 0;
    return pak_read(p->pak, name, out_buf, out_size);
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsPak* p = (HxfsPak*)self;
    if (!names || !count) return;
    if (!p || !p->pak) {
        *names = NULL;
        *count = 0;
        return;
    }
    pak_list(p->pak, names, count);
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    pak_free_list(names, count);
}

static void hxfs_close(hxAssetPlugin* self) {
    HxfsPak* p = (HxfsPak*)self;
    if (!p) return;
    if (p->pak) {
        pak_close(p->pak);
        p->pak = NULL;
    }
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsPak* p = (HxfsPak*)self;
    if (!p) return;
    hxfs_close(self);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    HxfsPak* p = (HxfsPak*)calloc(1, sizeof(HxfsPak));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "pak";
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
