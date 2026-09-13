/*
 * hxfs_pk3 — Helix IAssetPlugin for Quake 3 / ET PK3 (ZIP) archives.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "zip_file.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxAssetPlugin api;
    ZipFile* zip;
} HxfsPk3;

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsPk3* p = (HxfsPk3*)self;
    ZipFile* next;
    if (!p) return 0;
    if (p->zip) {
        zip_close(p->zip);
        p->zip = NULL;
    }
    if (!path || !path[0]) return 0;
    next = zip_open_file(path);
    if (!next) return 0;
    p->zip = next;
    return 1;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsPk3* p = (HxfsPk3*)self;
    if (!p || !p->zip) return 0;
    return zip_contains(p->zip, name);
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsPk3* p = (HxfsPk3*)self;
    if (!p || !p->zip) return 0;
    return zip_read(p->zip, name, out_buf, out_size);
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsPk3* p = (HxfsPk3*)self;
    if (!names || !count) return;
    if (!p || !p->zip) {
        *names = NULL;
        *count = 0;
        return;
    }
    zip_list(p->zip, names, count);
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    zip_free_list(names, count);
}

static void hxfs_close(hxAssetPlugin* self) {
    HxfsPk3* p = (HxfsPk3*)self;
    if (!p) return;
    if (p->zip) {
        zip_close(p->zip);
        p->zip = NULL;
    }
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsPk3* p = (HxfsPk3*)self;
    if (!p) return;
    hxfs_close(self);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    HxfsPk3* p = (HxfsPk3*)calloc(1, sizeof(HxfsPk3));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "pk3";
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
