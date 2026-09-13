/*
 * hxfs_ff — Helix IAssetPlugin for versioned CoD FastFiles (IW3/WaW/IW4).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "ff_file.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    hxAssetPlugin api;
    FfFile* ff;
} HxfsFf;

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsFf* p = (HxfsFf*)self;
    FfFile* next;
    if (!p) return 0;
    if (p->ff) {
        ff_close(p->ff);
        p->ff = NULL;
    }
    if (!path || !path[0]) return 0;
    next = ff_open_file(path);
    if (!next) return 0;
    p->ff = next;
    return 1;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsFf* p = (HxfsFf*)self;
    if (!p || !p->ff) return 0;
    return ff_contains(p->ff, name);
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsFf* p = (HxfsFf*)self;
    if (!p || !p->ff) return 0;
    return ff_read(p->ff, name, out_buf, out_size);
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsFf* p = (HxfsFf*)self;
    if (!names || !count) return;
    if (!p || !p->ff) {
        *names = NULL;
        *count = 0;
        return;
    }
    ff_list(p->ff, names, count);
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    (void)self;
    ff_free_list(names, count);
}

static void hxfs_close(hxAssetPlugin* self) {
    HxfsFf* p = (HxfsFf*)self;
    if (!p) return;
    if (p->ff) {
        ff_close(p->ff);
        p->ff = NULL;
    }
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsFf* p = (HxfsFf*)self;
    if (!p) return;
    hxfs_close(self);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    HxfsFf* p = (HxfsFf*)calloc(1, sizeof(HxfsFf));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "ff";
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
