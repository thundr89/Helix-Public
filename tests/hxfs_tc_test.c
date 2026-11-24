/*
 * Tests for the modular hxfs_tc plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "helix/fs/iasset_plugin.h"
#include "archive/archive.h"
#include "bsp/read.h"
#include "bsp/names.h"
#include "bsp/helix.h"
#include "cache.h"
#include "zip_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

hxAssetPlugin* helix_asset_plugin_create(void);

static int g_fail = 0;

static void expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    }
}

static int checkpoint(void) {
    return g_fail;
}

static void report(const char* name, int before) {
    printf("%s: %s\n", name, g_fail == before ? "PASS" : "FAIL");
}

static void test_plugin_create(void) {
    int before = checkpoint();
    hxAssetPlugin* p = helix_asset_plugin_create();
    expect(p != NULL, "helix_asset_plugin_create returned NULL");
    if (p) {
        expect(p->api_version == HELIX_ASSET_PLUGIN_API_VERSION, "api_version mismatch");
        expect(strcmp(p->format_name, "tc") == 0, "format_name is not tc");
        expect(p->open != NULL, "open is NULL");
        expect(p->open_dir != NULL, "open_dir is NULL");
        expect(p->open_many != NULL, "open_many is NULL");
        expect(p->contains != NULL, "contains is NULL");
        expect(p->read != NULL, "read is NULL");
        expect(p->free_buf != NULL, "free_buf is NULL");
        expect(p->list != NULL, "list is NULL");
        expect(p->free_list != NULL, "free_list is NULL");
        expect(p->close != NULL, "close is NULL");
        expect(p->destroy != NULL, "destroy is NULL");
        p->destroy(p);
    }
    report("test_plugin_create", before);
}

static void test_bsp_read_rejects_garbage(void) {
    int before = checkpoint();
    unsigned char garbage[256];
    BspLump lumps[BSP_LUMP_COUNT];
    memset(garbage, 0xCC, sizeof(garbage));
    expect(!bsp_read_valid(garbage, sizeof(garbage)), "garbage should not be valid IBSP v47");
    expect(!bsp_read_lumps(garbage, sizeof(garbage), lumps), "garbage lumps should fail");
    report("test_bsp_read_rejects_garbage", before);
}

static void test_bsp_names(void) {
    int before = checkpoint();
    char candidates[3][256];
    char map_name[64];
    char alias[256];
    int n;

    n = bsp_candidate_paths("maps/obj_snow.hxmap", candidates, 3, map_name, sizeof(map_name));
    expect(n == 3, "candidate count should be 3");
    expect(strcmp(map_name, "obj_snow") == 0, "map_name should be obj_snow");
    expect(strcmp(candidates[0], "maps/obj_snow.bsp") == 0, "candidates[0] should be maps/obj_snow.bsp");

    expect(bsp_hxmap_alias("maps/obj_snow.bsp", alias, sizeof(alias)), "alias should succeed");
    expect(strcmp(alias, "maps/obj_snow.hxmap") == 0, "alias should be maps/obj_snow.hxmap");
    report("test_bsp_names", before);
}

static void test_tc_directory_integration(void) {
    int before = checkpoint();
    const char* tc_dir = "C:/Users/tothd/Desktop/idTech3-4/build/truecombat";
    hxAssetPlugin* p;
    FILE* check = fopen("C:/Users/tothd/Desktop/idTech3-4/build/truecombat/pak3.pk3", "rb");
    if (!check) {
        printf("test_tc_directory_integration: SKIPPED (no TCE files)\n");
        return;
    }
    fclose(check);

    p = helix_asset_plugin_create();
    expect(p != NULL, "plugin create failed");
    if (p) {
        int opened = p->open_dir(p, tc_dir);
        expect(opened >= 4, "open_dir should open at least 4 pk3 archives");

        expect(p->contains(p, "maps/obj_snow.hxmap"), "contains maps/obj_snow.hxmap");
        expect(p->contains(p, "maps/obj_village.hxmap"), "contains maps/obj_village.hxmap");

        /* Test reading virtual .hxmap */
        {
            unsigned char* buf = NULL;
            unsigned int sz = 0;
            int ok = p->read(p, "maps/obj_snow.hxmap", &buf, &sz);
            expect(ok == 1, "read maps/obj_snow.hxmap failed");
            expect(buf != NULL && sz > 0, "hxmap buffer is empty");
            if (buf && sz > 0) {
                expect(strstr((const char*)buf, "map {\n") != NULL, "missing map { header");
                expect(strstr((const char*)buf, "info_player_deathmatch") != NULL, "missing info_player_deathmatch");
                expect(strstr((const char*)buf, "brush {") != NULL || strstr((const char*)buf, "mesh {") != NULL, "missing geometry");
                p->free_buf(p, buf);
            }
        }

        /* Test listing */
        {
            char** names = NULL;
            unsigned int count = 0;
            p->list(p, &names, &count);
            expect(names != NULL && count > 0, "list returned empty");
            if (names && count > 0) {
                int found_hxmap = 0;
                unsigned int i;
                for (i = 0; i < count; i++) {
                    if (strcmp(names[i], "maps/obj_snow.hxmap") == 0) found_hxmap = 1;
                }
                expect(found_hxmap, "list did not contain maps/obj_snow.hxmap");
                p->free_list(p, names, count);
            }
        }

        p->destroy(p);
    }
    report("test_tc_directory_integration", before);
}

int main(void) {
    printf("=== hxfs_tc_test ===\n");
    test_plugin_create();
    test_bsp_read_rejects_garbage();
    test_bsp_names();
    test_tc_directory_integration();
    printf("Result: %s (%d failures)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
