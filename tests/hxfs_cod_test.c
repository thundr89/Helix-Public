/*
 * Tests for the modular hxfs_cod plugin. Does not touch cod2003.dll.
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
#include "xmodel/read.h"
#include "xmodel/helix.h"
#include "xanim/read.h"
#include "xanim/helix.h"
#include "sound/read.h"
#include "sound/helix.h"
#include "ui/read.h"
#include "ui/helix.h"
#include "gsc/read.h"
#include "gsc/helix.h"
#include "gsc/vm.h"
#include "gsc/compile.h"
#include "shader/helix.h"
#include "cache.h"
#include "zip_file.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <zlib.h>

#ifdef _WIN32
#include <direct.h>
#endif

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
    expect(p != NULL, "create");
    if (!p) return;
    expect(p->api_version == HELIX_ASSET_PLUGIN_API_VERSION, "api version");
    expect(p->format_name && strcmp(p->format_name, "cod") == 0, "format cod");
    expect(p->contains(p,"ui/strings.hxloc"),"localization virtual file is discoverable");
    expect(p->open && p->contains && p->read && p->free_buf, "read fns");
    expect(p->list && p->free_list && p->close && p->destroy, "lifetime fns");
    expect(p->open_many && p->open_dir, "open fns");
    p->destroy(p);
    report("test_plugin_create", before);
}

static void test_archive_blob(void) {
    int before = checkpoint();
    CodArchive* a = cod_archive_create();
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    const char* hello = "hello";
    expect(a != NULL, "archive");
    expect(cod_archive_add_blob(a, "notes.txt", hello, 5) == 1, "add blob");
    expect(cod_archive_add_blob(a, "notes.txt", "world", 5) == 1, "replace blob");
    expect(cod_archive_contains(a, "notes.txt") == 1, "contains");
    expect(cod_archive_read(a, "NOTES.TXT", &buf, &sz) == 1, "read case");
    expect(sz == 5 && buf && memcmp(buf, "world", 5) == 0, "last blob wins");
    free(buf);
    cod_archive_destroy(a);
    report("test_archive_blob", before);
}

static void test_bsp_names_and_header(void) {
    int before = checkpoint();
    unsigned char hdr[8 + 33 * 8];
    char cands[3][256];
    char map[64];
    char alias[64];
    int n;
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "IBSP", 4);
    hdr[4] = 59;
    hdr[5] = 0;
    hdr[6] = 0;
    hdr[7] = 0;
    expect(bsp_read_valid(hdr, sizeof(hdr)) == 1, "ibsp magic");
    expect(bsp_read_valid(hdr, 4) == 0, "short header");
    n = bsp_candidate_paths("maps/mp_harbor.hxmap", cands, 3, map, sizeof(map));
    expect(n == 3, "three candidates");
    expect(strcmp(map, "mp_harbor") == 0, "map name");
    expect(strcmp(cands[0], "maps/mp/mp_harbor.bsp") == 0, "direct bsp");
    expect(strcmp(cands[1], "maps/mp/mp_harbor.bsp") == 0, "mp prefix");
    expect(strcmp(cands[2], "maps/mp/mp_harbor.bsp") == 0, "basename bsp");
    expect(bsp_hxmap_alias("maps/mp/mp_harbor.bsp", alias, sizeof(alias)) == 1, "alias");
    expect(strcmp(alias, "maps/mp/mp_harbor.hxmap") == 0, "alias keeps mp dir");
    expect(bsp_hxmap_alias("maps/mp/winter_passed.bsp", alias, sizeof(alias)) == 1, "winter mp alias");
    expect(strcmp(alias, "maps/mp/winter_passed.hxmap") == 0, "winter stays under maps/mp");
    expect(bsp_hxmap_alias("maps/winter_passed.bsp", alias, sizeof(alias)) == 0, "sp winter alias");
    expect(bsp_hxmap_alias("maps/credits.bsp", alias, sizeof(alias)) == 0, "credits alias");
    expect(bsp_hxmap_alias("maps/sp_harbor.bsp", alias, sizeof(alias)) == 0, "sp not aliased");
    expect(bsp_hxmap_alias("maps/prefabs/door.bsp", alias, sizeof(alias)) == 0, "prefab hidden");
    n = bsp_candidate_paths("maps/mp/winter_passed.hxmap", cands, 3, map, sizeof(map));
    expect(n == 3, "mp path candidates");
    expect(strcmp(cands[0], "maps/mp/winter_passed.bsp") == 0, "mp bsp same dir");
    expect(strcmp(cands[1], "maps/mp/winter_passed.bsp") == 0, "no sp fallback");
    expect(strcmp(cands[2], "maps/mp/winter_passed.bsp") == 0, "no basename fallback");
    report("test_bsp_names_and_header", before);
}

static void test_xmodel_through_archive(void) {
    int before = checkpoint();
    const char* text =
        "MODEL\n"
        "VERSION 5\n"
        "MATERIAL 0 \"textures/test/crate\"\n"
        "NUMVERTS 3\n"
        "VERT 0\n"
        "OFFSET 1 2 3\n"
        "UV 1 0.25 0.5\n"
        "VERT 1\n"
        "OFFSET 4 5 6\n"
        "UV 1 0 0\n"
        "VERT 2\n"
        "OFFSET 7 8 9\n"
        "UV 1 0 0\n"
        "NUMFACES 1\n"
        "TRI 0 0 0 1 2\n";
    CodArchive* a = cod_archive_create();
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    expect(cod_archive_add_blob(a, "xmodel/crate", text, (unsigned int)strlen(text)) == 1, "store text");
    expect(xmodel_to_helix(a, "xmodel/crate", &obj, &sz) == 1, "load text");
    expect(obj && strstr((const char*)obj, "v 1 2 3\n") != NULL, "obj vert");
    expect(obj && strstr((const char*)obj, "usemtl textures/test/crate\n") != NULL, "obj shader");
    free(obj);
    cod_archive_destroy(a);
    report("test_xmodel_through_archive", before);
}

static void put_u16(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_f32(unsigned char* p, float v) {
    memcpy(p, &v, 4);
}

static void test_xmodel_v14_lod(void) {
    int before = checkpoint();
    unsigned char header[64];
    char lod[64];
    size_t hoff = 0;
    memset(header, 0, sizeof(header));
    put_u16(header + hoff, 14);
    hoff += 2;
    hoff += 24;
    header[hoff++] = 0;
    header[hoff++] = 0;
    header[hoff++] = 0;
    header[hoff++] = 0;
    memcpy(header + hoff, "crate0", 7);
    hoff += 7;
    expect(cod_xmodel_lod_name(header, hoff, lod, sizeof(lod)) == 1, "lod");
    expect(strcmp(lod, "crate0") == 0, "crate0");
    report("test_xmodel_v14_lod", before);
}

static void test_view_basis_rigid(void) {
    int before = checkpoint();
    float axes[9];
    float trans[3];
    float out[12];
    float expect12[12] = {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, -1.f, 0.f, 0.f, -2.f, 3.f, 1.f};
    int i;
    memset(axes, 0, sizeof(axes));
    /* 90 degrees about CoD Z: (x, y) -> (-y, x). Columns are the images of X, Y, Z. */
    axes[0] = 0.f; axes[1] = 1.f; axes[2] = 0.f;
    axes[3] = -1.f; axes[4] = 0.f; axes[5] = 0.f;
    axes[6] = 0.f; axes[7] = 0.f; axes[8] = 1.f;
    trans[0] = 1.f; trans[1] = 2.f; trans[2] = 3.f;
    expect(cod_view_basis_rigid(axes, trans, out) == 1, "basis returns 1");
    for (i = 0; i < 12; i++) expect(out[i] == expect12[i], "basis component");
    expect(cod_view_basis_rigid(NULL, trans, out) == 0, "null axes");
    report("test_view_basis_rigid", before);
}

static void test_view_tag_from_parts(void) {
    int before = checkpoint();
    unsigned char blob[128];
    float out[12];
    size_t o = 0;
    int i;
    memset(blob, 0, sizeof(blob));
    put_u16(blob + o, 14); o += 2;
    put_u16(blob + o, 1); o += 2; /* one child */
    put_u16(blob + o, 1); o += 2; /* one root */
    blob[o++] = 0; /* parent is the root */
    put_f32(blob + o, 4.f); o += 4;
    put_f32(blob + o, 5.f); o += 4;
    put_f32(blob + o, 6.f); o += 4;
    o += 6; /* quat shorts stay 0 */
    memcpy(blob + o, "tag_view", 8); o += 8;
    blob[o++] = 0;
    o += 24; /* ignored tail, zeros */
    memcpy(blob + o, "tag_weapon", 10); o += 10;
    blob[o++] = 0;
    for (i = 0; i < 24; i++) blob[o++] = 0xab;
    expect(cod_xmodel_view_tag(blob, o, "tag_weapon", out) == 1, "tag found");
    expect(out[0] == 1.f && out[1] == 0.f && out[2] == 0.f, "x axis");
    expect(out[3] == 0.f && out[4] == 1.f && out[5] == 0.f, "y axis");
    expect(out[6] == 0.f && out[7] == 0.f && out[8] == 1.f, "z axis");
    expect(out[9] == -5.f && out[10] == 6.f && out[11] == 4.f, "permuted translation");
    expect(cod_xmodel_view_tag(blob, o, "tag_missing", out) == 0, "unknown tag");
    report("test_view_tag_from_parts", before);
}

static void test_viewhand_text(void) {
    int before = checkpoint();
    float tag[12] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 5.f, 6.f, 4.f};
    unsigned char* text = NULL;
    unsigned sz = 0;
    expect(cod_viewhand_text(tag, &text, &sz) == 1, "text");
    expect(text && strstr((char*)text, "hands xmodel/viewmodel_hands_whermact\n") != NULL, "hands line");
    expect(text && strstr((char*)text, "gun xmodel/viewmodel_mp44\n") != NULL, "gun line");
    expect(text && strstr((char*)text, "tag 1 0 0 0 1 0 0 0 1 5 6 4\n") != NULL, "tag line");
    free(text);
    report("test_viewhand_text", before);
}

/* Idle frame 0 replaces the bind local. The parts child sits at the origin. */
static void test_viewhand_idle_pose(void) {
    int before = checkpoint();
    unsigned char parts[128];
    unsigned char anim[64];
    float out[12];
    size_t o = 0;
    size_t parts_len;
    int i;
    memset(parts, 0, sizeof(parts));
    put_u16(parts + o, 14); o += 2;
    put_u16(parts + o, 1); o += 2;
    put_u16(parts + o, 1); o += 2;
    parts[o++] = 0;
    o += 12; /* translation stays 0 */
    o += 6;
    memcpy(parts + o, "tag_view", 8); o += 8;
    parts[o++] = 0;
    o += 24;
    memcpy(parts + o, "tag_weapon", 10); o += 10;
    parts[o++] = 0;
    o += 24;
    parts_len = o;
    expect(cod_xmodel_view_tag(parts, parts_len, "tag_weapon", out) == 1, "bind tag");
    expect(out[9] == 0.f && out[10] == 0.f && out[11] == 0.f, "bind stays at the eye");

    o = 0;
    memset(anim, 0, sizeof(anim));
    put_u16(anim + o, 14); o += 2;
    put_u16(anim + o, 1); o += 2; /* one frame */
    put_u16(anim + o, 1); o += 2; /* one part */
    anim[o++] = 0;                /* not looped, no delta */
    put_u16(anim + o, 30); o += 2;
    anim[o++] = 0; /* sign bits */
    anim[o++] = 0; /* full quaternion */
    memcpy(anim + o, "tag_weapon", 10); o += 10;
    anim[o++] = 0;
    put_u16(anim + o, 1); o += 2; /* one rotation key, identity shorts */
    o += 6;
    put_u16(anim + o, 1); o += 2;
    put_f32(anim + o, 4.f); o += 4;
    put_f32(anim + o, 5.f); o += 4;
    put_f32(anim + o, 6.f); o += 4;
    expect(cod_xmodel_set_anim_pose(anim, o) == 1, "pose stored");
    expect(cod_xmodel_view_tag(parts, parts_len, "tag_weapon", out) == 1, "posed tag");
    for (i = 0; i < 9; i++) {
        float ident = (i % 4 == 0) ? 1.f : 0.f;
        expect(out[i] > ident - 1e-4f && out[i] < ident + 1e-4f, "posed axis");
    }
    expect(out[9] > -5.f - 1e-4f && out[9] < -5.f + 1e-4f, "posed -ty");
    expect(out[10] > 6.f - 1e-4f && out[10] < 6.f + 1e-4f, "posed tz");
    expect(out[11] > 4.f - 1e-4f && out[11] < 4.f + 1e-4f, "posed tx");
    cod_xmodel_set_anim_pose(NULL, 0);
    expect(cod_xmodel_view_tag(parts, parts_len, "tag_weapon", out) == 1, "cleared tag");
    expect(out[9] == 0.f && out[10] == 0.f && out[11] == 0.f, "clear restores bind");
    report("test_viewhand_idle_pose", before);
}

/* Retail bind bones all sit on the origin. Idle frame 0 moves the arms. */
static void test_viewhand_idle_retail(void) {
    int before = checkpoint();
    const char* env = getenv("COD_MAIN");
    const char* dir = (env && env[0]) ? env :
        "C:/Program Files (x86)/Steam/steamapps/common/Call of Duty/Main";
    DIR* opened = opendir(dir);
    CodArchive* ar;
    char pak[1024];
    unsigned char* anim = NULL;
    unsigned char* parts = NULL;
    unsigned asz = 0, psz = 0;
    float xyz[256 * 3];
    int n, i;
    float far = 0.f;
    size_t dlen;
    if (!opened) {
        printf("test_viewhand_idle_retail: SKIP\n");
        return;
    }
    closedir(opened);
    ar = cod_archive_create();
    dlen = strlen(dir);
    if (dlen > 0 && (dir[dlen - 1] == '/' || dir[dlen - 1] == '\\'))
        snprintf(pak, sizeof(pak), "%spak0.pk3", dir);
    else
        snprintf(pak, sizeof(pak), "%s/pak0.pk3", dir);
    expect(ar && cod_archive_add_zip(ar, pak) == 1, "pak0");
    expect(ar && cod_archive_read(ar, "xanim/viewmodel_mp44_idle", &anim, &asz) == 1, "idle clip");
    expect(ar && cod_archive_read(ar, "xmodelparts/viewmodel_hands_new4", &parts, &psz) == 1, "hand parts");
    expect(cod_xmodel_set_anim_pose(anim, asz) == 1, "retail pose");
    free(anim);
    anim = NULL;
    asz = 0;
    expect(ar && cod_archive_read(ar, "xanim/viewmodel_mp44_ADS_up", &anim, &asz) == 1, "ads up clip");
    expect(cod_xmodel_overlay_anim_pose(anim, asz) == 1, "hip tag_torso");
    n = cod_xmodel_world_translations(parts, psz, xyz, 256);
    expect(n > 4, "bone count");
    for (i = 0; i < n; i++) {
        float ax = xyz[i * 3], ay = xyz[i * 3 + 1], az = xyz[i * 3 + 2];
        float m = ax < 0 ? -ax : ax;
        if (ay < 0 && -ay > m) m = -ay;
        if (az < 0 && -az > m) m = -az;
        if (ay > m) m = ay;
        if (az > m) m = az;
        if (m > far) far = m;
    }
    expect(far > 8.f, "an arm leaves the eye");
    {
        float tag[12];
        expect(cod_xmodel_view_tag(parts, psz, "tag_weapon", tag) == 1, "hip tag");
        /* ADS_up frame 0 pushes tag_torso to about (8.5, -3.3, -3.7). */
        expect(tag[11] > 5.f, "hip tag is in front of the eye");
        expect(tag[10] < -2.f, "hip tag is below the eye");
    }
    cod_xmodel_set_anim_pose(NULL, 0);
    free(anim);
    free(parts);
    cod_archive_destroy(ar);
    report("test_viewhand_idle_retail", before);
}

/* A second clip replaces its own bone and leaves the idle bone in place. */
static void test_viewhand_pose_overlay(void) {
    int before = checkpoint();
    unsigned char idle[64];
    unsigned char ads[64];
    unsigned char parts[160];
    float out[12];
    size_t o = 0;
    size_t parts_len;
    memset(parts, 0, sizeof(parts));
    put_u16(parts + o, 14); o += 2;
    put_u16(parts + o, 1); o += 2;
    put_u16(parts + o, 1); o += 2;
    parts[o++] = 0;
    o += 18;
    memcpy(parts + o, "tag_view", 8); o += 8;
    parts[o++] = 0;
    o += 24;
    memcpy(parts + o, "tag_weapon", 10); o += 10;
    parts[o++] = 0;
    o += 24;
    parts_len = o;

    o = 0;
    memset(idle, 0, sizeof(idle));
    put_u16(idle + o, 14); o += 2;
    put_u16(idle + o, 1); o += 2;
    put_u16(idle + o, 1); o += 2;
    idle[o++] = 0;
    put_u16(idle + o, 30); o += 2;
    idle[o++] = 0;
    idle[o++] = 0;
    memcpy(idle + o, "tag_weapon", 10); o += 10;
    idle[o++] = 0;
    put_u16(idle + o, 1); o += 2;
    o += 6;
    put_u16(idle + o, 1); o += 2;
    put_f32(idle + o, 4.f); o += 4;
    put_f32(idle + o, 5.f); o += 4;
    put_f32(idle + o, 6.f); o += 4;
    expect(cod_xmodel_set_anim_pose(idle, o) == 1, "idle stored");

    o = 0;
    memset(ads, 0, sizeof(ads));
    put_u16(ads + o, 14); o += 2;
    put_u16(ads + o, 1); o += 2;
    put_u16(ads + o, 1); o += 2;
    ads[o++] = 0;
    put_u16(ads + o, 30); o += 2;
    ads[o++] = 0;
    ads[o++] = 0;
    memcpy(ads + o, "tag_torso", 9); o += 9;
    ads[o++] = 0;
    put_u16(ads + o, 0); o += 2;
    put_u16(ads + o, 1); o += 2;
    put_f32(ads + o, 8.f); o += 4;
    put_f32(ads + o, 0.f); o += 4;
    put_f32(ads + o, 0.f); o += 4;
    expect(cod_xmodel_overlay_anim_pose(ads, o) == 1, "torso merged");
    expect(cod_xmodel_view_tag(parts, parts_len, "tag_weapon", out) == 1, "weapon kept");
    expect(out[11] > 4.f - 1e-4f && out[11] < 4.f + 1e-4f, "idle translation stays");
    expect(cod_xmodel_overlay_anim_pose(NULL, 0) == 0, "empty overlay rejected");
    expect(cod_xmodel_view_tag(parts, parts_len, "tag_weapon", out) == 1, "pose still there");
    expect(out[11] > 4.f - 1e-4f && out[11] < 4.f + 1e-4f, "reject leaves the pose");
    /* Sparse translation keys at 0 and 2 must interpolate frame 1. */
    o=0;memset(ads,0,sizeof(ads));
    put_u16(ads+o,14);o+=2;put_u16(ads+o,3);o+=2;put_u16(ads+o,1);o+=2;
    ads[o++]=0;put_u16(ads+o,30);o+=2;ads[o++]=0;ads[o++]=0;
    memcpy(ads+o,"tag_weapon",11);o+=11;
    put_u16(ads+o,0);o+=2;put_u16(ads+o,2);o+=2;
    ads[o++]=0;ads[o++]=2;
    put_f32(ads+o,0);o+=4;put_f32(ads+o,0);o+=4;put_f32(ads+o,0);o+=4;
    put_f32(ads+o,10);o+=4;put_f32(ads+o,0);o+=4;put_f32(ads+o,0);o+=4;
    expect(cod_xmodel_overlay_anim_frame(ads,o,1)==1,"sparse middle frame sampled");
    expect(cod_xmodel_view_tag(parts,parts_len,"tag_weapon",out)==1,"sampled sparse tag");
    expect(fabsf(out[11]-5)<.0001f,"sparse translation interpolates neighboring keys");
    expect(cod_xmodel_overlay_anim_frame(ads,o-1,1)==0,"truncated sparse clip rejected");
    expect(cod_xmodel_view_tag(parts,parts_len,"tag_weapon",out)==1 && fabsf(out[11]-5)<.0001f,"invalid frame preserves valid pose");
    cod_xmodel_remove_anim_bone("tag_weapon");
    expect(cod_xmodel_view_tag(parts,parts_len,"tag_weapon",out)==1 && fabsf(out[11])<.0001f,"attachment root removed without clearing clip");
    cod_xmodel_set_anim_pose(NULL, 0);
    report("test_viewhand_pose_overlay", before);
}

static void test_xmodel_lod_slots(void) {
    int before = checkpoint();
    unsigned char header[128];
    CodXmodelLod slots[3];
    size_t hoff = 0;
    int n;
    memset(header, 0, sizeof(header));
    put_u16(header + hoff, 14);
    hoff += 2;
    hoff += 24; /* mins/maxs */
    put_f32(header + hoff, 1000.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank0", 18); hoff += 18;
    put_f32(header + hoff, 2500.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank_medLOD0", 25); hoff += 25;
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "flak88_anti-tank_lowLOD0", 25); hoff += 25;
    n = cod_xmodel_lod_slots(header, hoff, slots, 3);
    expect(n == 3, "three slots");
    expect(slots[0].distance == 1000.f, "near distance");
    expect(strcmp(slots[0].name, "flak88_anti-tank0") == 0, "near name");
    expect(slots[2].distance == 0.f, "low distance stays 0");
    expect(strcmp(slots[2].name, "flak88_anti-tank_lowLOD0") == 0, "low name");
    expect(cod_xmodel_lod_name(header, hoff, slots[0].name, 64) == 1, "first name still works");
    report("test_xmodel_lod_slots", before);
}

static void test_xmodel_first_lod(void) {
    int before = checkpoint();
    CodArchive* a = cod_archive_create();
    unsigned char header[96];
    unsigned char hi[20 + 96];
    unsigned char lo[20 + 96];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    size_t hoff = 0;
    size_t i;
    memset(header, 0, sizeof(header));
    memset(hi, 0, sizeof(hi));
    memset(lo, 0, sizeof(lo));
    put_u16(header + hoff, 14); hoff += 2; hoff += 24;
    put_f32(header + hoff, 1000.f); hoff += 4;
    memcpy(header + hoff, "hi0", 4); hoff += 4;
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "lo0", 4); hoff += 4;
    put_u16(hi, 14); put_u16(hi + 2, 1);
    hi[5] = 3; hi[7] = 1; hi[13] = 3;
    put_u16(hi + 16, 1); put_u16(hi + 18, 2);
    put_u16(lo, 14); put_u16(lo + 2, 1);
    lo[5] = 3; lo[7] = 1; lo[13] = 3;
    put_u16(lo + 16, 1); put_u16(lo + 18, 2);
    for (i = 0; i < 3; i++) {
        float p[3];
        p[0] = 10.f; p[1] = 0.f; p[2] = 0.f;
        memcpy(hi + 20 + i * 32 + 20, p, 12);
        p[0] = 99.f;
        memcpy(lo + 20 + i * 32 + 20, p, 12);
    }
    expect(cod_archive_add_blob(a, "xmodel/truck", header, (unsigned)hoff) == 1, "xm");
    expect(cod_archive_add_blob(a, "xmodelsurfs/lo0", lo, sizeof(lo)) == 1, "lo");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 1, "fallback lod");
    expect(obj && strstr((const char*)obj, "v 99 ") != NULL, "low surf when hi is missing");
    free(obj); obj = NULL;
    expect(cod_archive_add_blob(a, "xmodelsurfs/hi0", hi, sizeof(hi)) == 1, "hi");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 1, "near lod");
    expect(obj && strstr((const char*)obj, "v 10 ") != NULL, "first named surf");
    expect(obj && strstr((const char*)obj, "v 99 ") == NULL, "low surf not chosen by density");
    free(obj);
    obj = NULL;
    expect(xmodel_to_helix(a, "xmodel/missing", &obj, &sz) == 0, "unknown model fails");
    cod_archive_destroy(a);
    /* Header only, no surf blobs: conversion fails. */
    a = cod_archive_create();
    expect(cod_archive_add_blob(a, "xmodel/truck", header, (unsigned)hoff) == 1, "xm only");
    expect(xmodel_to_helix(a, "xmodel/truck", &obj, &sz) == 0, "no readable slot fails");
    free(obj);
    cod_archive_destroy(a);
    report("test_xmodel_first_lod", before);
}

static size_t write_skin_xmodel(unsigned char* xm, size_t cap, const char** names, int count) {
    size_t n = 0;
    int i;
    memset(xm, 0, cap);
    put_u16(xm + n, 14);
    n += 2 + 24;
    put_f32(xm + n, 0.f);
    n += 4;
    if (n + 6 > cap) return 0;
    memcpy(xm + n, "surf0", 6);
    n += 6;
    put_f32(xm + n, 0.f);
    n += 4;
    if (n >= cap) return 0;
    xm[n++] = 0;
    put_f32(xm + n, 0.f);
    n += 4;
    if (n >= cap) return 0;
    xm[n++] = 0;
    n += 8;
    if (n + 2 > cap) return 0;
    put_u16(xm + n, (unsigned short)count);
    n += 2;
    for (i = 0; i < count; i++) {
        size_t len = strlen(names[i]) + 1;
        if (n + len > cap) return 0;
        memcpy(xm + n, names[i], len);
        n += len;
    }
    return n;
}

static void test_xmodel_v14_surf(void) {
    int before = checkpoint();
    unsigned char xm[2];
    unsigned char surf[20 + 3 * 32];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    size_t i;
    memset(surf, 0, sizeof(surf));
    put_u16(xm, 14);
    put_u16(surf + 0, 14);
    put_u16(surf + 2, 1);
    surf[5] = 3; /* num verts, after the unknown byte at [4] */
    surf[7] = 1; /* num tris */
    surf[13] = 3; /* strip length */
    put_u16(surf + 16, 1);
    put_u16(surf + 18, 2);
    for (i = 0; i < 3; i++) {
        float pos[3];
        pos[0] = (float)i;
        pos[1] = 1.f;
        pos[2] = 2.f;
        memcpy(surf + 20 + i * 32 + 20, pos, sizeof(pos));
    }
    expect(cod_xmodel_to_obj(xm, sizeof(xm), surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "v14 obj");
    expect(obj && strstr((const char*)obj, "v 0 1 2\n") != NULL, "v14 vert");
    expect(obj && strstr((const char*)obj, "f 1/1/1 3/3/3 2/2/2\n") != NULL, "v14 face");
    expect(cod_gameplay_xmodel("models/view_mg.obj") != NULL &&
               strstr(cod_gameplay_xmodel("models/view_mg.obj"), "viewmodel_m1garand") != NULL,
           "view mg is the m1 garand");
    expect(cod_gameplay_xmodel("models/player.obj") != NULL, "player body");
    expect(cod_gameplay_xmodel("models/pickup.obj") == NULL, "pickup stays stock");
    {
        unsigned char skinxm[192];
        const char* skins[2];
        size_t skin_n;
        skins[0] = "body@characterhand.dds";
        skins[1] = "viewmodel@bar_body.dds";
        skin_n = write_skin_xmodel(skinxm, sizeof(skinxm), skins, 2);
        free(obj);
        obj = NULL;
        sz = 0;
        expect(cod_xmodel_to_obj(skinxm, skin_n, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "skin obj");
        expect(obj && strstr((const char*)obj, "usemtl skins/body@characterhand.dds\n") != NULL, "first file-order skin");
    }
    free(obj);
    report("test_xmodel_v14_surf", before);
}

static void fill_one_tri(unsigned char* surf) {
    size_t i;
    memset(surf, 0, 20 + 3 * 32);
    put_u16(surf + 0, 14);
    put_u16(surf + 2, 1);
    surf[5] = 3;
    surf[7] = 1;
    surf[13] = 3;
    put_u16(surf + 14, 0);
    put_u16(surf + 16, 1);
    put_u16(surf + 18, 2);
    for (i = 0; i < 3; i++) {
        float pos[3];
        pos[0] = (float)i;
        pos[1] = 1.f;
        pos[2] = 2.f;
        memcpy(surf + 20 + i * 32 + 20, pos, sizeof(pos));
    }
}

static void test_xmodel_skin_order(void) {
    int before = checkpoint();
    unsigned char xm[256];
    unsigned char surf[20 + 3 * 32];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    char names[4][160];
    const char* pair[2];
    const char* hands[2];
    size_t n;
    int count;
    fill_one_tri(surf);
    pair[0] = "metal@ford.dds";
    pair[1] = "metal@fordhub.dds";
    n = write_skin_xmodel(xm, sizeof(xm), pair, 2);
    count = cod_xmodel_skin_names(xm, n, names, 4);
    expect(count == 2, "two lod skins");
    expect(strcmp(names[0], "metal@ford.dds") == 0, "file order");
    expect(strcmp(names[1], "metal@fordhub.dds") == 0, "second skin kept");
    cod_xmodel_set_keep_hands(0);
    expect(cod_xmodel_to_obj(xm, n, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "prop convert");
    expect(obj && strstr((const char*)obj, "usemtl skins/metal@ford.dds\n") != NULL, "first skin");
    expect(obj && strstr((const char*)obj, "characterhand") == NULL, "prop path does not sort");
    free(obj);
    obj = NULL;
    put_u16(xm, 14);
    expect(cod_xmodel_to_obj(xm, 2, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "no skins still converts");
    expect(obj && strstr((const char*)obj, "usemtl skins/unbound.png\n") != NULL, "unbound");
    free(obj);
    obj = NULL;
    hands[0] = "body@characterhand.dds";
    hands[1] = "viewmodel@bar_body.dds";
    n = write_skin_xmodel(xm, sizeof(xm), hands, 2);
    cod_xmodel_set_keep_hands(1);
    expect(cod_xmodel_to_obj(xm, n, surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "viewmodel skins");
    expect(obj && strstr((const char*)obj, "usemtl skins/viewmodel@bar_body.dds\n") != NULL, "hand sorted behind flag");
    free(obj);
    cod_xmodel_set_keep_hands(0);
    report("test_xmodel_skin_order", before);
}

static void test_sound_gameplay_alias(void) {
    int before = checkpoint();
    CodArchive* a = cod_archive_create();
    const char* base = "sound \"other\" {\n}\n";
    unsigned char* buf = (unsigned char*)malloc(strlen(base) + 1);
    unsigned int sz = (unsigned int)strlen(base);
    memcpy(buf, base, sz + 1);
    expect(cod_archive_add_blob(a, "sound/weapons/bar/bar_01.wav", "x", 1) == 1, "wav blob");
    expect(sound_append_gameplay(a, &buf, &sz) == 1, "append");
    expect(buf && strstr((char*)buf, "sound \"weapon/mg\"") != NULL, "mg alias");
    expect(buf && strstr((char*)buf, "sound/weapons/bar/bar_01.wav") != NULL, "mg file");
    expect(buf && strstr((char*)buf, "weapon/sg") == NULL, "sg skipped");
    free(buf);
    cod_archive_destroy(a);
    report("test_sound_gameplay_alias", before);
}

static void test_xanim(void) {
    int before = checkpoint();
    const char* text = "ANIMATION\nVERSION 1\n";
    unsigned char bin[4];
    unsigned char* out = NULL;
    unsigned int sz = 0;
    CodArchive* a = cod_archive_create();
    bin[0] = 14;
    bin[1] = 0;
    bin[2] = 1;
    bin[3] = 2;
    expect(xanim_identify(text, strlen(text)) == XANIM_TEXT, "text kind");
    expect(xanim_to_helix_text(text, strlen(text), &out, &sz) == 1, "text copy");
    expect(sz == strlen(text) && out && memcmp(out, text, sz) == 0, "text bytes");
    free(out);
    out = NULL;
    expect(xanim_identify(bin, sizeof(bin)) == XANIM_BINARY, "binary kind");
    expect(xanim_binary_version(bin, sizeof(bin)) == 14, "version 14");
    expect(xanim_to_helix_text(bin, sizeof(bin), &out, &sz) == 0, "binary is not text");
    expect(out == NULL, "no fake clip");
    expect(cod_archive_add_blob(a, "xanim/idle", bin, sizeof(bin)) == 1, "store anim");
    expect(xanim_to_helix(a, "xanim/idle", &out, &sz) == 1, "load raw");
    expect(sz == 4 && out && memcmp(out, bin, 4) == 0, "raw bytes unchanged");
    free(out);
    cod_archive_destroy(a);
    report("test_xanim", before);
}

static void test_plugin_bsp_open(void) {
    int before = checkpoint();
    hxAssetPlugin* p = helix_asset_plugin_create();
    unsigned char hdr[8 + 33 * 8];
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    const char* path = "hxfs_cod_test_stub.bsp";
    FILE* f;
    char** names = NULL;
    unsigned int count = 0;
    unsigned int i;
    int saw_bsp = 0, saw_hx = 0;
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "IBSP", 4);
    hdr[4] = 59;
    hdr[5] = 0;
    hdr[6] = 0;
    hdr[7] = 0;
    f = fopen(path, "wb");
    expect(f != NULL, "write stub");
    if (f) {
        fwrite(hdr, 1, sizeof(hdr), f);
        fclose(f);
    }
    expect(p && p->open(p, path) == 1, "open bsp");
    expect(p && p->contains(p, "maps/hxfs_cod_test_stub.bsp") == 1, "contains bsp");
    expect(p && p->contains(p, "maps/hxfs_cod_test_stub.hxmap") == 0, "sp hxmap hidden");
    expect(p && p->read(p, "maps/hxfs_cod_test_stub.bsp", &buf, &sz) == 1, "read bsp");
    expect(sz == sizeof(hdr) && buf && memcmp(buf, hdr, sizeof(hdr)) == 0, "bsp bytes");
    if (p) p->free_buf(p, buf);
    if (p) p->list(p, &names, &count);
    for (i = 0; i < count; i++) {
        if (strcmp(names[i], "maps/hxfs_cod_test_stub.hxmap") == 0) saw_hx = 1;
    }
    expect(!saw_hx, "sp bsp is not a map");
    if (p) p->free_list(p, names, count);
    names = NULL;
    count = 0;
    saw_hx = 0;

    remove(path);
    {
        const char* mp = "mp_stub.bsp";
        f = fopen(mp, "wb");
        if (f) {
            fwrite(hdr, 1, sizeof(hdr), f);
            fclose(f);
        }
        expect(p && p->open(p, mp) == 1, "open mp bsp");
        expect(p && p->contains(p, "maps/mp/mp_stub.hxmap") == 1, "mp hxmap visible");
        if (p) p->list(p, &names, &count);
        for (i = 0; i < count; i++) {
            if (strcmp(names[i], "maps/mp/mp_stub.bsp") == 0) saw_bsp = 1;
            if (strcmp(names[i], "maps/mp/mp_stub.hxmap") == 0) saw_hx = 1;
        }
        expect(saw_bsp && saw_hx, "list alias");
        if (p) p->free_list(p, names, count);
        remove(mp);
    }
    {
        const char* winter = "winter_passed.bsp";
        int saw_winter = 0;
        f = fopen(winter, "wb");
        if (f) {
            fwrite(hdr, 1, sizeof(hdr), f);
            fclose(f);
        }
        expect(p && p->open(p, winter) == 1, "open winter");
        expect(p && p->contains(p, "maps/winter_passed.bsp") == 1, "winter bsp kept");
        expect(p && p->contains(p, "maps/winter_passed.hxmap") == 0, "winter not a map");
        names = NULL;
        count = 0;
        if (p) p->list(p, &names, &count);
        for (i = 0; i < count; i++) {
            if (strcmp(names[i], "maps/winter_passed.hxmap") == 0) saw_winter = 1;
        }
        expect(!saw_winter, "winter not listed as a map");
        if (p) p->free_list(p, names, count);
        remove(winter);
    }
    if (p) p->destroy(p);
    report("test_plugin_bsp_open", before);
}

static int test_menu_include(void* user, const char* path,
                             unsigned char** data, unsigned int* size) {
    const char* body =
        "#define OPTIONS_WINDOW_POS 8 9\n"
        "#define OPTIONS_WINDOW_SIZE 360 325\n";
    size_t n;
    (void)user;
    if (!path || strcmp(path, "ui/menudef.h") != 0 || !data || !size) return 0;
    n = strlen(body);
    *data = (unsigned char*)malloc(n);
    if (!*data) return 0;
    memcpy(*data, body, n);
    *size = (unsigned int)n;
    return 1;
}

static void test_sound_ui_gsc(void) {
    int before = checkpoint();
    const char* csv =
        "name,sequence,file,vol_min,vol_max,dist_min,dist_max,channel,type,loop\n"
        "# note\n"
        "ambient_mp_harbor,1,ambient/harbor.wav,0.6,0.8,120,2000,local,streamed,looping\n";
    const char* menu =
        "menuDef {\n"
        "  name \"main\"\n"
        "  fullscreen 1\n"
        "  rect 0 0 640 480\n"
        "  itemDef {\n"
        "    name \"play\"\n"
        "    type ITEM_TYPE_BUTTON\n"
        "    rect 10 20 100 30\n"
        "    text \"PLAY\"\n"
        "    textalign ITEM_ALIGN_CENTER\n"
        "    action { open \"select\" ; setcvar \"g_gametype\" \"dm\" ; setcvar \"g_timelimit\" \"15\" ; setcvar \"scr_foo\" \"1\" ; }\n"
        "  }\n"
        "  itemDef {\n"
        "    name \"logo\"\n"
        "    type ITEM_TYPE_IMAGE\n"
        "    rect 0 0 64 64\n"
        "    image \"ui_mp/logo\"\n"
        "  }\n"
        "  itemDef {\n"
        "    name \"score\"\n"
        "    type ITEM_TYPE_OWNERDRAW\n"
        "    text \"SCORE\"\n"
        "    action { play \"click\" ; }\n"
        "  }\n"
        "}\n";
    const char* gsc =
        "main()\n"
        "{\n"
        "  maps\\mp\\_load::main();\n"
        "  level.fraglimit = 30;\n"
        "  level.gametype = \"tdm\";\n"
        "  level.timelimit = getcvar(\"scr_dm_timelimit\");\n"
        "  ambientPlay(\"ambient_mp_harbor\");\n"
        "}\n";
    SoundAliasFile aliases;
    unsigned char* out = NULL;
    unsigned int sz = 0;
    const char* text;
    unsigned char wav[12];
    memcpy(wav, "RIFF", 4);
    memcpy(wav + 8, "WAVE", 4);
    expect(sound_read_is_wav(wav, sizeof(wav)) == 1, "wav");
    expect(sound_read_aliases(csv, strlen(csv), &aliases) == 1, "csv");
    expect(aliases.count == 1, "one alias");
    expect(aliases.count == 1 && strcmp(aliases.aliases[0].file, "ambient/harbor.wav") == 0, "cod path");
    expect(aliases.count == 1 && aliases.aliases[0].looping == 1, "looping");
    expect(aliases.count == 1 && aliases.aliases[0].dist_max == 2000.f, "dist max");
    expect(sound_to_helix(&aliases, &out, &sz) == 1, "sound helix");
    text = (const char*)out;
    expect(text && strstr(text, "sound \"ambient_mp_harbor\"") != NULL, "shader name");
    expect(text && strstr(text, "file \"sound/ambient/harbor.wav\"") != NULL, "sound prefix");
    expect(text && strstr(text, "looping 1") != NULL, "helix loop");
    expect(text && strstr(text, "volume 0.6") != NULL, "vol min");
    free(out);
    sound_read_free(&aliases);

    expect(ui_menu_to_helix(menu, strlen(menu), &out, &sz) == 1, "menu");
    text = (const char*)out;
    expect(text && strstr(text, "type button") != NULL, "button type");
    expect(text && strstr(text, "textalign center") != NULL, "align");
    expect(text && strstr(text, "open \"select\"") != NULL, "open");
    expect(text && strstr(text, "set \"g_gametype\" \"dm\"") != NULL, "setcvar");
    expect(text && strstr(text, "set \"g_timelimit\" \"15\"") != NULL, "timelimit cvar");
    expect(text && strstr(text, "set \"scr_foo\" \"1\"") != NULL, "preserve valid mod cvar action");
    expect(text && strstr(text, "type image") != NULL, "image kept");
    expect(text && strstr(text, "SCORE") != NULL, "ownerdraw kept");
    expect(text && strstr(text, "play \"click\"") != NULL, "ownerdraw play kept");
    free(out);

    {
        const char* codmain =
            "menuDef {\n"
            "  name \"main\"\n"
            "  fullScreen MENU_TRUE\n"
            "  rect 0 0 640 480\n"
            "  onOpen { close mods_menu; }\n"
            "  itemDef {\n"
            "    name \"credit\"\n"
            "    type ITEM_TYPE_BUTTON\n"
            "    decoration\n"
            "    backcolor 0 0 0 0\n"
            "    text \"@MENU_CREDITS\"\n"
            "    action { exec \"map credits\" ; exec \"spmap training\" ; }\n"
            "  }\n"
            "  itemDef {\n"
            "    name join\n"
            "    type ITEM_TYPE_BUTTON\n"
            "    text \"@MENU_JOIN_GAME\"\n"
            "    action { open joinserver ; play \"mouse_click\" ; }\n"
            "  }\n"
            "}\n";
        expect(ui_menu_to_helix(codmain, strlen(codmain), &out, &sz) == 1, "cod main parses");
        text = (const char*)out;
        expect(text && strstr(text, "fullscreen 1") != NULL, "MENU_TRUE");
        expect(text && strstr(text, "close \"mods_menu\"") != NULL, "unquoted close");
        expect(text && strstr(text, "open \"joinserver\"") != NULL, "unquoted open");
        expect(text && strstr(text, "Join Game") != NULL, "menu label");
        expect(text && strstr(text, "name \"join\"") != NULL, "unquoted name");
        expect(text && strstr(text, "map credits") == NULL, "no credits map");
        expect(text && strstr(text, "spmap") == NULL, "no spmap");
        free(out);
        {
            const char* str =
                "REFERENCE           BACKTOGAME\n"
                "LANG_ENGLISH        \"Back To Game\"\n";
            const char* labeled =
                "menuDef {\n"
                "  name main\n"
                "  itemDef { name resume type ITEM_TYPE_BUTTON text \"@MENU_BACKTOGAME\" }\n"
                "}\n";
            expect(ui_loc_add_str("menu", str, strlen(str)) == 1, "str");
            {unsigned char* exported=NULL;unsigned int length=0;
             expect(ui_loc_export(&exported,&length)==1,"runtime localization export");
             expect(exported && strstr((const char*)exported,"MENU_BACKTOGAME") && strstr((const char*)exported,"Back To Game"),"runtime localization retains package key and value");free(exported);}

            expect(ui_menu_to_helix(labeled, strlen(labeled), &out, &sz) == 1, "loc menu");
            text = (const char*)out;
            expect(text && strstr(text, "Back To Game") != NULL, "localized label");
            expect(text && strstr(text, "@MENU_") == NULL, "alias replaced");
            free(out);
        }
        {
            const char* defined =
                "#include \"ui/menudef.h\"\n"
                "\\\\ comment that is not a key\n"
                "menuDef {\n"
                "  name options_credits\n"
                "  rect OPTIONS_WINDOW_POS OPTIONS_WINDOW_SIZE\n"
                "  itemDef {\n"
                "    name credit_back\n"
                "    type ITEM_TYPE_BUTTON\n"
                "    text \"@MENU_CREDITS\"\n"
                "    rect OPTIONS_WINDOW_POS OPTIONS_WINDOW_SIZE\n"
                "    decoration\n"
                "  }\n"
                "}\n";
            ui_menu_set_include(test_menu_include, NULL);
            expect(ui_menu_to_helix(defined, strlen(defined), &out, &sz) == 1, "macro menu");
            ui_menu_set_include(NULL, NULL);
            text = (const char*)out;
            expect(text && strstr(text, "rect 8 9 360 325") != NULL, "include rect");
            expect(text && strstr(text, "OPTIONS_WINDOW") == NULL, "macros expanded");
            expect(text && strstr(text, "name \"credit_back\"") != NULL, "flag item kept");
            free(out);
        }
        {
            const char* cs_menu =
                "menuDef {\n"
                "  name \"createserver\"\n"
                "  fullscreen 1\n"
                "  onOpen { open createserver_op ; open createserver_maps ; }\n"
                "  onESC { close createserver_maps ; close createserver_op ; close createserver ; open main ; }\n"
                "}\n"
                "menuDef {\n"
                "  name \"createserver_maps\"\n"
                "  rect 404 137 200 300\n"
                "  itemDef {\n"
                "    name mappreview\n"
                "    type ITEM_TYPE_OWNERDRAW\n"
                "    ownerdraw UI_STARTMAPCINEMATIC\n"
                "    rect 1 -5 191 142\n"
                "  }\n"
                "  itemDef {\n"
                "    name maplist\n"
                "    type ITEM_TYPE_LISTBOX\n"
                "    feeder FEEDER_ALLMAPS\n"
                "    rect -1 134 192 130\n"
                "  }\n"
                "}\n";
            expect(ui_menu_to_helix(cs_menu, strlen(cs_menu), &out, &sz) == 1, "createserver menu");
            text = (const char*)out;
            expect(text && strstr(text, "open \"createserver_op\"") != NULL, "cs opens op");
            expect(text && strstr(text, "open \"createserver_maps\"") != NULL, "cs opens maps");
            expect(text && strstr(text, "name \"mappreview\"") != NULL, "cs mappreview");
            expect(text && strstr(text, "type image") != NULL, "cs preview image");
            expect(text && strstr(text, "feeder \"maps\"") != NULL, "cs map list");
            expect(text && strstr(text, "cvar \"ui_map\"") != NULL, "cs map cvar");
            expect(text && strstr(text, "rect 404 137 200 300") != NULL, "cs maps panel");
            expect(text && strstr(text, "rect -1 -5 192 142") != NULL, "preview shares list edge");
            expect(text && strstr(text, "cvarTest \"ui_server_tab\"") == NULL, "no invented tabs");
            free(out);
        }
        {
            const char* tabs =
                "menuDef {\n"
                "  name \"createserver_op\"\n"
                "  onOpen { hide advancedsettings ; show generalsettings ; }\n"
                "  itemDef {\n"
                "    name button_gametypesettings\n"
                "    type ITEM_TYPE_BUTTON\n"
                "    text \"Game Type\"\n"
                "    group gametypesettings\n"
                "    action { hide generalsettings ; openForGameType \"settings_%s\" ; }\n"
                "  }\n"
                "}\n";
            expect(ui_menu_to_helix(tabs, strlen(tabs), &out, &sz) == 1, "tab groups");
            text = (const char*)out;
            expect(text && strstr(text, "hide \"advancedsettings\"") != NULL, "hides advanced");
            expect(text && strstr(text, "show \"generalsettings\"") != NULL, "shows general");
            expect(text && strstr(text, "group \"gametypesettings\"") != NULL, "tab group");
            expect(text && strstr(text, "openforgametype \"settings_%s\"") != NULL, "opens gametype page");
            free(out);
        }
        {
            const char* mod_menu =
                "menuDef {\n"
                "  name \"mods_menu\"\n"
                "  rect 5 75 360 325\n"
                "  itemDef {\n"
                "    name modlist\n"
                "    type ITEM_TYPE_LISTBOX\n"
                "    feeder FEEDER_MODS\n"
                "    rect 8 30 351 220\n"
                "    textalignx 0\n"
                "    textaligny 2\n"
                "  }\n"
                "  itemDef {\n"
                "    name launch\n"
                "    type ITEM_TYPE_BUTTON\n"
                "    text \"@MENU_LAUNCH\"\n"
                "    action { uiScript RunMod ; }\n"
                "  }\n"
                "}\n";
            expect(ui_menu_to_helix(mod_menu, strlen(mod_menu), &out, &sz) == 1, "mods menu");
            text = (const char*)out;
            expect(text && strstr(text, "name \"mods_menu\"") != NULL, "mods menuDef");
            expect(text && strstr(text, "feeder \"codmods\"") != NULL, "stock mods list");
            expect(text && strstr(text, "feeder \"helixmods\"") == NULL, "no second helix list");
            expect(text && strstr(text, "cvar \"ui_helix_mod\"") != NULL, "mods selection cvar");
            expect(text && strstr(text, "exec \"load_mod\"") != NULL, "launch loads mod");
            expect(text && strstr(text, "textalignx 0") != NULL, "text anchor x");
            expect(text && strstr(text, "rect 5 75 360 325") != NULL, "cod mods window");
            expect(text && strstr(text, "rect 380 75 250 325") == NULL, "no extra helix window");
            free(out);
        }
        {
            const char* gfx =
                "menuDef {\n"
                "  name \"options_graphics\"\n"
                "  itemDef {\n"
                "    name mode\n"
                "    type ITEM_TYPE_MULTI\n"
                "    text \"@MENU_VIDEO_MODE\"\n"
                "    cvar \"ui_r_mode\"\n"
                "    rect 5 80 350 12\n"
                "    textalignx 185\n"
                "    textaligny 11\n"
                "  }\n"
                "}\n"
                "menuDef {\n"
                "  name \"options_performance\"\n"
                "  itemDef {\n"
                "    name sync\n"
                "    type ITEM_TYPE_YESNO\n"
                "    text \"@MENU_SYNC_EVERY_FRAME\"\n"
                "    cvar \"r_swapinterval\"\n"
                "    rect 5 115 350 12\n"
                "  }\n"
                "}\n"
                "menuDef {\n"
                "  name \"options_move\"\n"
                "  itemDef {\n"
                "    name fwd\n"
                "    type ITEM_TYPE_BIND\n"
                "    text \"@MENU_FORWARD\"\n"
                "    cvar \"+forward\"\n"
                "    rect 5 40 350 12\n"
                "  }\n"
                "}\n"
                "menuDef {\n"
                "  name \"options_look\"\n"
                "  itemDef {\n"
                "    name inv\n"
                "    type ITEM_TYPE_YESNO\n"
                "    text \"@MENU_INVERT_MOUSE\"\n"
                "    cvar \"ui_mousePitch\"\n"
                "    rect 5 115 350 13\n"
                "  }\n"
                "}\n";
            {const char* empty="menuDef {\n name \"options_graphics\"\n rect 0 0 400 400\n itemDef {\n name title\n type ITEM_TYPE_TEXT\n text \"Graphics\"\n rect 0 0 200 20\n decoration\n }\n itemDef {\n name quality\n type ITEM_TYPE_MULTI\n cvar \"ui_r_picmip\"\n rect 0 80 200 15\n }\n}\n";
             unsigned char* menu_out=NULL;unsigned int menu_size=0;
             expect(ui_menu_to_helix(empty,strlen(empty),&menu_out,&menu_size),"graphics rows without cvars translate safely");
             expect(menu_out && strstr((char*)menu_out,"ui_textureQuality") && !strstr((char*)menu_out,"cvar \"r_picmip\""),"legacy quality row replaced by preset");free(menu_out);}
            {
                const char* unsupported[]={"r_allowExtensions","r_vertexlight","r_ati_pntriangles","r_ext_compressed_textures","ui_glCustom","r_fastsky","cg_skybox","ai_corpsecount","cg_coronafardist","cg_wolfparticles"};
                unsigned k;
                for(k=0;k<sizeof(unsupported)/sizeof(unsupported[0]);k++){
                    char source[1400],binding[160];unsigned char* converted=NULL;unsigned converted_size=0;
                    snprintf(source,sizeof(source),"menuDef {\n name %s\n itemDef {\n name legacy\n type ITEM_TYPE_MULTI\n text \"Legacy:\"\n cvar %s\n rect 5 100 350 13\n action { exec \"legacy_renderer_command\"; }\n }\n itemDef {\n name custom\n type ITEM_TYPE_MULTI\n cvar mod_quality\n rect 5 120 350 13\n }\n}\n",k<5?"options_graphics":"options_performance",unsupported[k]);
                    snprintf(binding,sizeof(binding),"cvar \"%s\"",unsupported[k]);
                    expect(ui_menu_to_helix(source,strlen(source),&converted,&converted_size),"stock unsupported row converts");
                    expect(converted && !strstr((char*)converted,binding) && !strstr((char*)converted,"legacy_renderer_command"),"unused stock control has no binding or executable action");
                    expect(converted && strstr((char*)converted,"hx_legacy_value_") && strstr((char*)converted,"decoration 1 scrollable 1"),"stock status stays in scrolling layout without hit target");
                    expect(converted && strstr((char*)converted,"cvar \"mod_quality\""),"unknown mod option remains interactive");
                    expect(converted && strstr((char*)converted,k==3?"Automatic":k==4?"Manual":"Unavailable"),"stock row describes effective support");
                    free(converted);
                }
            }
            expect(ui_menu_to_helix(gfx, strlen(gfx), &out, &sz) == 1, "options rows");
            text = (const char*)out;
            expect(text && strstr(text, "cvar \"ui_vidmode\"") != NULL, "resolution uses helix mode");
            expect(text && strstr(text, "vid_mode_next") != NULL, "resolution cycles");
            expect(text && strstr(text, "cvar \"r_vsync\"") != NULL, "vsync on sync row");
            expect(text && strstr(text, "ui_bind_forward") != NULL, "forward bind cvar");
            expect(text && strstr(text, "bind_action forward") != NULL, "forward rebinds");
            expect(text && strstr(text, "cvar \"m_invert\"") != NULL, "invert on look row");
            expect(text && strstr(text, "cvar \"r_aces\"") != NULL, "aces in graphics");
            expect(text && strstr(text, "cvar \"r_borderless\"") != NULL, "borderless in graphics");
            expect(text && strstr(text, "cvar \"r_physical_falloff\"") != NULL, "falloff in graphics");
            expect(text && strstr(text, "cvar \"r_renderer\"") != NULL, "renderer in graphics");
            expect(text && strstr(text,"name options_graphics_status") && strstr(text,"ui_gfx_texture_storage_requested") && strstr(text,"ui_gfx_texture_storage_effective") && strstr(text,"ui_gfx_texture_storage_reason"),"graphics status exposes requested effective and restriction data");
            expect(text && strstr(text,"ui_renderer_requested") && strstr(text,"ui_renderer_effective") && strstr(text,"ui_renderer_reason"),"graphics status separates renderer request from active fallback");
            expect(text && strstr(text,"open options_graphics_status") && strstr(text,"open options_graphics ;"),"graphics status navigation returns to graphics");
            expect(text && strstr(text,"opengl33") && strstr(text,"OpenGL 3.3"),"GL33 renderer in graphics");
#ifdef _WIN32
            expect(text && strstr(text,"d3d9") && strstr(text,"d3d11"),"legacy Direct3D renderers in Windows graphics menu");
#else
            expect(text && !strstr(text,"d3d9") && !strstr(text,"d3d11"),"Direct3D renderers excluded from Linux graphics menu");
#endif
            expect(text && !strstr(text,"hx_texture_options") && strstr(text,"ui_textureQuality") && strstr(text,"\"Low\"") && strstr(text,"\"Normal\"") && strstr(text,"\"High\"") && !strstr(text,"r_textureMipBias") && !strstr(text,"r_textureMip\"") && strstr(text,"r_bumpScale") && strstr(text,"writeconfig") && strstr(text,"apply_system_settings") && strstr(text,"scrollRegion") && strstr(text,"codStyle 1"),"integrated system options use CoD style scrolling apply and save");
            expect(text && strstr(text, "cvar \"ui_gamepad\"") != NULL, "controller on look");
            free(out);
        }
    }

    expect(gsc_source_to_helix(gsc, strlen(gsc), &out, &sz) == 1, "gsc");
    text = (const char*)out;
    expect(text && strstr(text, "level.fraglimit = 30;") != NULL, "fraglimit");
    expect(text && strstr(text, "level.gametype = \"tdm\";") != NULL, "gametype");
    expect(text && strstr(text, "level.teamplay = 1;") != NULL, "tdm teamplay");
    expect(text && strstr(text, "level.timelimit") == NULL, "skip getcvar");
    expect(text && strstr(text, "level.ambient = \"ambient_mp_harbor\";") != NULL, "ambient literal");
    free(out);
    {
        const char* harbor =
            "main()\n{\n"
            "  game[\"allies\"] = \"russian\";\n"
            "  game[\"axis\"] = \"german\";\n"
            "  game[\"russian_soldiertype\"] = \"conscript\";\n"
            "  game[\"russian_soldiervariation\"] = \"winter\";\n"
            "  game[\"german_soldiertype\"] = \"waffen\";\n"
            "  game[\"german_soldiervariation\"] = \"winter\";\n"
            "}\n";
        GscFile file;
        char body[160];
        expect(gsc_read(harbor, strlen(harbor), &file) == 1, "harbor gsc");
        expect(gsc_allied_body(&file, body, sizeof(body)) == 1, "allied body");
        expect(strcmp(body, "xmodel/playerbody_russian_conscript_winter") == 0, "russian winter");
        gsc_read_free(&file);
        cod_set_player_body(body);
        expect(strcmp(cod_gameplay_xmodel("models/player.obj"),
                      "xmodel/playerbody_russian_conscript_winter") == 0,
               "player alias");
        cod_set_player_body(NULL);
        expect(strcmp(cod_gameplay_xmodel("models/player.obj"),
                      "xmodel/playerbody_american_airborne") == 0,
               "default body");
    }
    report("test_sound_ui_gsc", before);
}

static void test_texture_shader_menu(void) {
    int before = checkpoint();
    unsigned int sz = 0;
    const char* shader =
        "textures/test/wall\n"
        "{\n"
        "  qer_editorimage textures/test/wall.tga\n"
        "  {\n"
        "    map textures/test/wall.tga\n"
        "  }\n"
        "}\n"
        "textures/test/floor\n"
        "{\n"
        "  {\n"
        "    map clamp textures/test/bare\n"
        "  }\n"
        "}\n";
    const char* menus =
        "{\n"
        "  loadmenu \"ui_mp/good.menu\"\n"
        "  loadmenu \"ui_mp/owner.menu\"\n"
        "}\n";
    const char* good =
        "menuDef {\n"
        "  name \"good\"\n"
        "  itemDef { name \"go\" type ITEM_TYPE_BUTTON text \"GO\" action { open \"main\" ; } }\n"
        "}\n";
    const char* owner =
        "menuDef {\n"
        "  name \"owner\"\n"
        "  itemDef { name \"od\" type ITEM_TYPE_OWNERDRAW text \"NO\" }\n"
        "}\n";
    unsigned char* text = NULL;
    CodArchive* a;
    expect(shader_source_to_helix(shader, strlen(shader), &text, &sz) == 1, "shader");
    expect(text && strstr((char*)text, "material textures/test/wall") != NULL, "mtr name");
    expect(text && strstr((char*)text, "albedo_map textures/test/bare\n") != NULL, "clamp path");
    expect(text && strstr((char*)text, "albedo_map textures/test/wall.tga") != NULL, "source extension");
    free(text);
    {
        const char* rich =
            "textures/test/leaf\n"
            "{\n"
            "  qer_editorimage textures/test/editor.tga\n"
            "  cull disable\n"
            "  {\n"
            "    map textures/test/leaf.tga\n"
            "    blendFunc blend\n"
            "    alphaFunc GE128\n"
            "  }\n"
            "}\n"
            "textures/common/caulk\n"
            "{\n"
            "  surfaceparm caulk\n"
            "  {\n"
            "    map textures/common/caulk.tga\n"
            "  }\n"
            "}\n";
        text = NULL;
        expect(shader_source_to_helix(rich, strlen(rich), &text, &sz) == 1, "rich shader");
        expect(text && strstr((char*)text, "albedo_map textures/test/leaf.tga") != NULL, "map not editor");
        expect(text && strstr((char*)text, "editor.tga") == NULL, "editor image dropped");
        expect(text && strstr((char*)text, "twoSided") != NULL, "two sided");
        expect(text && strstr((char*)text, "blend alpha") != NULL, "blend");
        expect(text && strstr((char*)text, "alphaTest 0.5") != NULL, "alpha test");
        expect(text && strstr((char*)text, "nodraw") != NULL, "caulk nodraw");
        expect(shader_is_invisible("textures/common/caulk") == 1, "caulk noted");
        free(text);
        text = NULL;
    }
    {
        const char* deck =
            "textures/battleship/flagfore\n"
            "{\n"
            "  {\n"
            "    map textures/battleship/whiteplanks.tga\n"
            "  nextbundle\n"
            "    map textures/battleship/deckflag_np.tga\n"
            "    tcMod transform .25 0 0 .25 .375 .4375\n"
            "  }\n"
            "  {\n"
            "    map $lightmap\n"
            "    blendFunc filter\n"
            "  }\n"
            "  {\n"
            "    perlight\n"
            "    map textures/battleship/whiteplanks.tga\n"
            "    blendFunc add\n"
            "  nextbundle\n"
            "    map textures/battleship/deckflag_np.tga\n"
            "    tcMod transform .25 0 0 .25 .375 .4375\n"
            "    blendFunc filter\n"
            "  }\n"
            "}\n";
        text = NULL;
        expect(shader_source_to_helix(deck, strlen(deck), &text, &sz) == 1, "deck flag shader");
        expect(text && strstr((char*)text, "albedo_map textures/battleship/deckflag_np.tga") != NULL, "deck flag image");
        expect(text && strstr((char*)text, "uvTransform 0.25 0 0 0.25 0.375 0.4375") != NULL, "deck flag uv");
        expect(text && strstr((char*)text, "blend additive") == NULL, "perlight add is not the base");
        free(text);
        text = NULL;
    }
    a = cod_archive_create();
    expect(cod_archive_add_blob(a, "ui_mp/menus.txt", menus, (unsigned int)strlen(menus)) == 1, "menus blob");
    expect(cod_archive_add_blob(a, "ui_mp/good.menu", good, (unsigned int)strlen(good)) == 1, "good menu");
    expect(cod_archive_add_blob(a, "ui_mp/owner.menu", owner, (unsigned int)strlen(owner)) == 1, "owner menu");
    {
        hxAssetPlugin* p = helix_asset_plugin_create();
        unsigned char* out = NULL;
        /* Drive the translator through the plugin by opening nothing and using the archive API
           via a loose file the plugin can open. */
        (void)p;
        if (p) p->destroy(p);
    }
    text = NULL;
    {
        /* menus_txt is reached through plugin read; build a one-file pk3 is heavier.
           Call the same menu filter the list uses: owner-only returns 0, good returns text. */
        expect(ui_menu_to_helix(owner, strlen(owner), &text, &sz) == 1, "owner menu kept");
        expect(text && strstr((char*)text, "NO") != NULL, "owner caption");
        free(text);
        text = NULL;
        expect(ui_menu_to_helix(good, strlen(good), &text, &sz) == 1, "good menu kept");
        free(text);
    }
    cod_archive_destroy(a);
    (void)menus;
    report("test_texture_shader_menu", before);
}

static void wr_u16(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
}

static void wr_u32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}

typedef struct ZipItem {
    const char* name;
    const void* data;
    unsigned size;
} ZipItem;

static int write_stored_pk3(const char* path, const ZipItem* files, int n) {
    unsigned char* buf;
    unsigned total = 22;
    unsigned pos;
    unsigned cd;
    int i;
    FILE* f;
    for (i = 0; i < n; i++) total += 30u + (unsigned)strlen(files[i].name) + files[i].size;
    for (i = 0; i < n; i++) total += 46u + (unsigned)strlen(files[i].name);
    buf = (unsigned char*)calloc(1, total);
    if (!buf) return 0;
    pos = 0;
    for (i = 0; i < n; i++) {
        unsigned namelen = (unsigned)strlen(files[i].name);
        unsigned crc = (unsigned)crc32(0L, files[i].size ? (const Bytef*)files[i].data : Z_NULL, files[i].size);
        wr_u32(buf + pos, 0x04034b50u);
        wr_u16(buf + pos + 4, 20);
        wr_u16(buf + pos + 8, 0);
        wr_u32(buf + pos + 14, crc);
        wr_u32(buf + pos + 18, files[i].size);
        wr_u32(buf + pos + 22, files[i].size);
        wr_u16(buf + pos + 26, namelen);
        memcpy(buf + pos + 30, files[i].name, namelen);
        if (files[i].size) memcpy(buf + pos + 30 + namelen, files[i].data, files[i].size);
        pos += 30u + namelen + files[i].size;
    }
    cd = pos;
    for (i = 0; i < n; i++) {
        unsigned namelen = (unsigned)strlen(files[i].name);
        unsigned crc = (unsigned)crc32(0L, files[i].size ? (const Bytef*)files[i].data : Z_NULL, files[i].size);
        unsigned local = 0;
        int k;
        for (k = 0; k < i; k++) local += 30u + (unsigned)strlen(files[k].name) + files[k].size;
        wr_u32(buf + pos, 0x02014b50u);
        wr_u16(buf + pos + 4, 20);
        wr_u16(buf + pos + 6, 20);
        wr_u32(buf + pos + 16, crc);
        wr_u32(buf + pos + 20, files[i].size);
        wr_u32(buf + pos + 24, files[i].size);
        wr_u16(buf + pos + 28, namelen);
        wr_u32(buf + pos + 42, local);
        memcpy(buf + pos + 46, files[i].name, namelen);
        pos += 46u + namelen;
    }
    wr_u32(buf + pos, 0x06054b50u);
    wr_u16(buf + pos + 8, (unsigned)n);
    wr_u16(buf + pos + 10, (unsigned)n);
    wr_u32(buf + pos + 12, pos - cd);
    wr_u32(buf + pos + 16, cd);
    f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return 0;
    }
    if (fwrite(buf, 1, total, f) != total) {
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);
    free(buf);
    return 1;
}

static char g_cache_env[600];

static void set_cache_env(const char* dir) {
    snprintf(g_cache_env, sizeof(g_cache_env), "HXFS_CACHE_DIR=%s", dir ? dir : "");
#ifdef _WIN32
    _putenv(g_cache_env);
#else
    if (dir && dir[0]) setenv("HXFS_CACHE_DIR", dir, 1);
    else unsetenv("HXFS_CACHE_DIR");
#endif
}

static void test_cvar_menu_and_cache(void) {
    int before = checkpoint();
    const char* gated =
        "menuDef {\n"
        "  name main\n"
        "  fullscreen 1\n"
        "  onESC { ingameclose main ; }\n"
        "  itemDef {\n"
        "    name resume\n"
        "    type ITEM_TYPE_BUTTON\n"
        "    text \"Resume\"\n"
        "    cvarTest cl_ingame\n"
        "    showCvar { \"1\" }\n"
        "    hideCvar { \"0\" }\n"
        "    action { ingameclose main ; uiScript \"quit\" ; uiScript StartServer ; }\n"
        "  }\n"
        "  itemDef {\n"
        "    rect 0 0 640 480\n"
        "    background \"ui/art\"\n"
        "    decoration\n"
        "  }\n"
        "}\n";
    const char* alpha =
        "menuDef { name alpha itemDef { name go type ITEM_TYPE_BUTTON text \"Alpha\" } }\n";
    const char* beta =
        "menuDef { name beta itemDef { name go type ITEM_TYPE_BUTTON text \"Beta\" } }\n";
    unsigned char tga[21];
    unsigned char dds[140];
    unsigned char* out = NULL;
    unsigned sz = 0;
    unsigned crc = 0;
    char dir[512];
    char pk3[600];
    char cache_path[1024];
    const char* menus_src =
        "{\n"
        "  loadMenu { \"ui_mp/missing.menu\" }\n"
        "  loadMenu { \"ui_mp/gate.menu\" }\n"
        "}\n";
    const char* tmp;
    hxAssetPlugin* plugin = NULL;
    ZipFile* zip = NULL;
    CodArchive* archive;
    ZipItem items[4];

    expect(ui_menu_to_helix(gated, strlen(gated), &out, &sz) == 1, "gated menu");
    expect(out && strstr((char*)out, "cvarTest \"cl_ingame\"") != NULL, "cvarTest");
    expect(out && strstr((char*)out, "showCvar \"1\"") != NULL, "showCvar");
    expect(out && strstr((char*)out, "hideCvar \"0\"") != NULL, "hideCvar");
    expect(out && strstr((char*)out, "ingameclose \"main\"") != NULL, "ingameclose");
    expect(out && strstr((char*)out, "exec \"quit\"") != NULL, "uiscript quit");
    expect(out && strstr((char*)out, "start_server 0") && strstr((char*)out, "start_server 1") && strstr((char*)out, "start_server 2"), "uiscript startserver dispatches listen/LAN/dedicated modes");
    expect(out && strstr((char*)out, "type image") != NULL, "background image");
    expect(out && strstr((char*)out, "image \"ui/art\"") != NULL, "background path");
    free(out);
    out = NULL;

    archive = cod_archive_create();
    expect(cod_archive_add_blob(archive, "ui_mp/gate.menu", alpha, (unsigned)strlen(alpha)) == 1, "blob");
    expect(cod_archive_checksum(archive, "ui_mp/gate.menu", &crc) == 1, "blob crc");
    expect(crc == (unsigned)crc32(0L, (const Bytef*)alpha, (uInt)strlen(alpha)), "blob crc32");
    cod_archive_destroy(archive);

    memset(tga, 0, sizeof(tga));
    tga[2] = 2;
    tga[12] = 1;
    tga[14] = 1;
    tga[16] = 24;
    tga[18] = 10;
    tga[19] = 20;
    tga[20] = 30;
    memset(dds, 0, sizeof(dds));
    memcpy(dds, "DDS ", 4);
    dds[8] = 124;
    dds[12] = 4;
    dds[16] = 4;
    dds[80] = 0x4;
    memcpy(dds + 84, "DXT1", 4);
    dds[128] = 0xff;
    dds[129] = 0xff;
    dds[130] = 0xff;
    dds[131] = 0xff;

    tmp = getenv("TEMP");
    if (!tmp || !tmp[0]) tmp = getenv("TMP");
    if (!tmp || !tmp[0]) tmp = ".";
    snprintf(dir, sizeof(dir), "%s/hxfs_cache_test", tmp);
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    set_cache_env(dir);
    snprintf(pk3, sizeof(pk3), "%s/gate.pk3", dir);
    cod_cache_path(dir, "ui_mp/gate.menu", cache_path, sizeof(cache_path));
    remove(cache_path);
    {
        char side[1100];
        snprintf(side, sizeof(side), "%s.crc", cache_path);
        remove(side);
    }
    {
        char path[1024];
        unsigned char* buf = NULL;
        unsigned n = 0;
        unsigned char mag[4];
        FILE* f;
        cod_cache_store(dir, "legacy.bin", 0x11u, "hello", 5);
        expect(cod_cache_load(dir, "legacy.bin", 0x11u, &buf, &n) == 1, "plain cache");
        expect(buf && n == 5 && memcmp(buf, "hello", 5) == 0, "plain body");
        free(buf);
        buf = NULL;
        cod_cache_path(dir, "legacy.bin", path, sizeof(path));
        f = fopen(path, "rb");
        expect(f != NULL, "plain file");
        if (f) {
            expect(fread(mag, 1, 4, f) == 4, "plain head");
            fclose(f);
            expect(!(mag[0] == 'H' && mag[1] == 'X' && mag[2] == 'C' && mag[3] == 'A'), "no HXCA prefix");
            f = fopen(path, "wb");
            if (f) {
                const unsigned char legacy[] = {'H', 'X', 'C', 'A', 1, 2, 3, 4, 'x'};
                fwrite(legacy, 1, sizeof(legacy), f);
                fclose(f);
            }
        }
        expect(cod_cache_load(dir, "legacy.bin", 0x11u, &buf, &n) == 0, "legacy prefix miss");
        free(buf);
    }

    items[0].name = "ui_mp/gate.menu";
    items[0].data = alpha;
    items[0].size = (unsigned)strlen(alpha);
    items[1].name = "ui/art.tga";
    items[1].data = tga;
    items[1].size = (unsigned)sizeof(tga);
    items[2].name = "ui/plate.dds";
    items[2].data = dds;
    items[2].size = (unsigned)sizeof(dds);
    items[3].name = "ui_mp/menus.txt";
    items[3].data = menus_src;
    items[3].size = (unsigned)strlen(menus_src);
    expect(write_stored_pk3(pk3, items, 4) == 1, "write pk3");

    zip = zip_open_file(pk3);
    expect(zip != NULL, "zip open");
    if (zip) {
        unsigned zcrc = 0;
        expect(zip_checksum(zip, "ui_mp/gate.menu", &zcrc) == 1, "zip crc");
        expect(zcrc == (unsigned)crc32(0L, (const Bytef*)alpha, (uInt)strlen(alpha)), "zip crc matches");
        zip_close(zip);
    }

    plugin = helix_asset_plugin_create();
    expect(plugin && plugin->open(plugin, pk3) == 1, "plugin open");
    if (plugin && plugin->contains(plugin, "ui/art.png")) {
        expect(plugin->read(plugin, "ui/art.png", &out, &sz) == 1, "tga via png name");
        expect(out && sz == sizeof(tga) && memcmp(out, tga, sizeof(tga)) == 0, "tga bytes unchanged");
        plugin->free_buf(plugin, out);
        out = NULL;
    } else {
        expect(0, "contains tga alias");
    }
    if (plugin && plugin->contains(plugin, "ui/plate.png")) {
        expect(plugin->read(plugin, "ui/plate.png", &out, &sz) == 1, "dds via png name");
        expect(out && sz == sizeof(dds) && memcmp(out, dds, sizeof(dds)) == 0, "dds bytes unchanged");
        plugin->free_buf(plugin, out);
        out = NULL;
    } else {
        expect(0, "contains dds alias");
    }
    if (plugin) {
        expect(plugin->read(plugin, "ui_mp/gate.menu", &out, &sz) == 1, "menu translate");
        expect(out && strstr((char*)out, "name \"alpha\"") != NULL, "alpha");
        plugin->free_buf(plugin, out);
        out = NULL;
        expect(plugin->read(plugin, "ui_mp/menus.txt", &out, &sz) == 1, "menus.txt");
        expect(out && strstr((char*)out, "ui_mp/gate.menu") != NULL, "listed menu");
        expect(out && strstr((char*)out, "missing.menu") == NULL, "missing menu omitted");
        plugin->free_buf(plugin, out);
        out = NULL;
        {
            unsigned char mag[4];
            FILE* f = fopen(cache_path, "rb");
            expect(f != NULL, "cache file");
            if (f) {
                const char stale[] = "STALE";
                expect(fread(mag, 1, 4, f) == 4, "cache head");
                fclose(f);
                expect(!(mag[0] == 'H' && mag[1] == 'X' && mag[2] == 'C' && mag[3] == 'A'), "menu cache is text");
                f = fopen(cache_path, "wb");
                if (f) {
                    fwrite(stale, 1, 5, f);
                    fclose(f);
                }
            }
        }
        expect(plugin->read(plugin, "ui_mp/gate.menu", &out, &sz) == 1, "cache hit");
        expect(out && sz == 5 && memcmp(out, "STALE", 5) == 0, "cached bytes");
        plugin->free_buf(plugin, out);
        out = NULL;
        items[0].data = beta;
        items[0].size = (unsigned)strlen(beta);
        expect(write_stored_pk3(pk3, items, 3) == 1, "rewrite pk3");
        expect(plugin->open(plugin, pk3) == 1, "reopen");
        expect(plugin->read(plugin, "ui_mp/gate.menu", &out, &sz) == 1, "reparse");
        expect(out && strstr((char*)out, "name \"beta\"") != NULL, "checksum miss");
        expect(out && strstr((char*)out, "STALE") == NULL, "not the stale cache");
        plugin->free_buf(plugin, out);
        plugin->destroy(plugin);
    }
    set_cache_env("");
    report("test_cvar_menu_and_cache", before);
}

static char g_vm_ambient[64];
static char g_vm_weapon[64];
static char g_vm_gt[32];

static void vm_ambient(void* user, const char* name) {
    (void)user;
    snprintf(g_vm_ambient, sizeof(g_vm_ambient), "%s", name ? name : "");
}
static void vm_weapon(void* user, const char* name) {
    (void)user;
    snprintf(g_vm_weapon, sizeof(g_vm_weapon), "%s", name ? name : "");
}

/* Sequence log for test_gsc_vm_thread only. vm_weapon stays last-name. */
#define VM_WEAPON_SEQ 8
static char g_vm_weapon_seq[VM_WEAPON_SEQ][64];
static int g_vm_weapon_seq_n;

static void vm_weapon_seq(void* user, const char* name) {
    (void)user;
    if (g_vm_weapon_seq_n < VM_WEAPON_SEQ) {
        snprintf(g_vm_weapon_seq[g_vm_weapon_seq_n],
                 sizeof(g_vm_weapon_seq[0]), "%s", name ? name : "");
        g_vm_weapon_seq_n++;
    }
}

static int weapon_seq_has(const char* name) {
    int i;
    for (i = 0; i < g_vm_weapon_seq_n; i++) {
        if (strcmp(g_vm_weapon_seq[i], name) == 0) return 1;
    }
    return 0;
}
static void vm_gt(void* user, const char* name) {
    (void)user;
    snprintf(g_vm_gt, sizeof(g_vm_gt), "%s", name ? name : "");
}

static void test_gsc_vm(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  level.gametype = \"tdm\";\n"
        "  ambientPlay(\"ambient_mp_harbor\");\n"
        "  giveWeapon(\"m1garand_mp\");\n"
        "  giveWeapon(\"kar98k_mp\");\n"
        "}\n";
    char names[4][64];
    int n;
    GscHost host;
    memset(&host, 0, sizeof(host));
    host.ambient = vm_ambient;
    host.give_weapon = vm_weapon;
    host.set_gametype = vm_gt;
    g_vm_ambient[0] = g_vm_weapon[0] = g_vm_gt[0] = '\0';
    n = gsc_vm_scan_weapons(src, strlen(src), names, 4);
    expect(n == 2, "two weapons");
    expect(n >= 1 && strcmp(names[0], "m1garand_mp") == 0, "garand");
    expect(n >= 2 && strcmp(names[1], "kar98k_mp") == 0, "kar98k");
    expect(gsc_vm_exec(src, strlen(src), "main", &host) == 1, "exec main");
    expect(strcmp(g_vm_ambient, "ambient_mp_harbor") == 0, "ambient");
    expect(strcmp(g_vm_weapon, "kar98k_mp") == 0, "last weapon");
    expect(strcmp(g_vm_gt, "tdm") == 0, "gametype");
    report("test_gsc_vm", before);
}

static void test_gsc_vm_fib(void) {
    int before = checkpoint();
    const char* src =
        "fib(n)\n"
        "{\n"
        "  if (n < 2)\n"
        "    return n;\n"
        "  return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "main()\n"
        "{\n"
        "  return fib(10);\n"
        "}\n";
    const char* wrap = "main()\n{\n  return 2147483648;\n}\n";
    const char* divmin = "main()\n{\n  return -2147483648 / -1;\n}\n";
    expect(gsc_vm_exec(src, strlen(src), "main", NULL) == 1, "fib runs");
    expect(gsc_vm_last_int == 55, "fib 10");
    expect(gsc_vm_exec(wrap, strlen(wrap), "main", NULL) == 1, "wrap runs");
    expect(gsc_vm_last_int == (int)(-2147483647 - 1), "2147483648 wraps");
    expect(gsc_vm_exec(divmin, strlen(divmin), "main", NULL) == 1, "divmin runs");
    expect(gsc_vm_last_int == (int)(-2147483647 - 1), "INT_MIN divided by -1");
    expect(gsc_vm_exec(src, strlen(src), "missing", NULL) == 0, "missing func");
    report("test_gsc_vm_fib", before);
}

static void test_gsc_vm_array(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  a = [];\n"
        "  for (i = 0; i < 5; i++)\n"
        "    a[i] = i * i;\n"
        "  total = 0;\n"
        "  for (j = 0; j < a.size; j++)\n"
        "    total += a[j];\n"
        "  s = spawnstruct();\n"
        "  s.total = total;\n"
        "  return s.total;\n"
        "}\n";
    const char* hang =
        "main()\n"
        "{\n"
        "  while (1)\n"
        "    i = 1;\n"
        "}\n";
    expect(gsc_vm_exec(src, strlen(src), "main", NULL) == 1, "array runs");
    expect(gsc_vm_last_int == 30, "sum of squares");
    const char* sw =
        "main()\n"
        "{\n"
        "  n = 1;\n"
        "  switch (n)\n"
        "  {\n"
        "  case 1:\n"
        "    n = n + 10;\n"
        "  case 2:\n"
        "    n = n + 100;\n"
        "    break;\n"
        "  default:\n"
        "    n = 0;\n"
        "  }\n"
        "  return n;\n"
        "}\n";
    expect(gsc_vm_exec(hang, strlen(hang), "main", NULL) == 0, "fuel stops the loop");
    expect(gsc_vm_exec(sw, strlen(sw), "main", NULL) == 1, "switch runs");
    expect(gsc_vm_last_int == 111, "switch falls through");
    const char* named =
        "main()\n"
        "{\n"
        "  n = 1;\n"
        "  switch (n)\n"
        "  {\n"
        "  showcase = 1;\n"
        "  case 1:\n"
        "    n = n + 10;\n"
        "  case 2:\n"
        "    n = n + 100;\n"
        "    break;\n"
        "  default:\n"
        "    n = 0;\n"
        "  }\n"
        "  return n;\n"
        "}\n";
    const char* huge =
        "main()\n"
        "{\n"
        "  a = [];\n"
        "  a[100000000] = 1;\n"
        "  return 7;\n"
        "}\n";
    expect(gsc_vm_exec(named, strlen(named), "main", NULL) == 1, "showcase switch runs");
    expect(gsc_vm_last_int == 111, "showcase is not a case");
    expect(gsc_vm_exec(huge, strlen(huge), "main", NULL) == 1, "huge index runs");
    expect(gsc_vm_last_int == 7, "huge index is a no-op");
    report("test_gsc_vm_array", before);
}

static char g_far_text[] =
    "note()\n"
    "{\n"
    "  x = \"sentinel\";\n"
    "}\n"
    "stock()\n"
    "{\n"
    "  giveWeapon(\"mp44_mp\");\n"
    "}\n"
    "ret()\n"
    "{\n"
    "  return 44;\n"
    "}\n"
    "ret2()\n"
    "{\n"
    "  a = [];\n"
    "  a[0] = 7;\n"
    "  return a;\n"
    "}\n"
    "take(a)\n"
    "{\n"
    "  giveWeapon(a[0]);\n"
    "}\n";

static int far_read(void* user, const char* path, char** text, size_t* size) {
    (void)user;
    if (!path || strcmp(path, "maps/mp/_load.gsc") != 0) return 0;
    *size = strlen(g_far_text);
    *text = (char*)malloc(*size + 1);
    if (!*text) return 0;
    memcpy(*text, g_far_text, *size + 1);
    return 1;
}

static void test_gsc_vm_far(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  maps\\mp\\_load::stock();\n"
        "}\n";
    GscHost host;
    memset(&host, 0, sizeof(host));
    host.read_file = far_read;
    host.give_weapon = vm_weapon;
    g_vm_weapon[0] = '\0';
    expect(gsc_vm_exec(src, strlen(src), "main", &host) == 1, "far runs");
    expect(strcmp(g_vm_weapon, "mp44_mp") == 0, "far weapon");
    {
        const char* iret =
            "main()\n"
            "{\n"
            "  return maps\\mp\\_load::ret();\n"
            "}\n";
        const char* aret =
            "main()\n"
            "{\n"
            "  return maps\\mp\\_load::ret2()[0];\n"
            "}\n";
        expect(gsc_vm_exec(iret, strlen(iret), "main", &host) == 1, "far int runs");
        expect(gsc_vm_last_int == 44, "far int return");
        expect(gsc_vm_exec(aret, strlen(aret), "main", &host) == 1, "far array runs");
        expect(gsc_vm_last_int == 7, "far array element");
    }
    {
        const char* div =
            "main()\n"
            "{\n"
            "  a = 8;\n"
            "  return a / 2;\n"
            "}\n";
        const char* take =
            "main()\n"
            "{\n"
            "  a = [];\n"
            "  a[0] = \"mp44_mp\";\n"
            "  maps\\mp\\_load::take(a);\n"
            "}\n";
        expect(gsc_vm_exec(div, strlen(div), "main", NULL) == 1, "div runs");
        expect(gsc_vm_last_int == 4, "a / 2");
        g_vm_weapon[0] = '\0';
        expect(gsc_vm_exec(take, strlen(take), "main", &host) == 1, "far arg runs");
        expect(strcmp(g_vm_weapon, "mp44_mp") == 0, "far array arg");
    }
    report("test_gsc_vm_far", before);
}

static void test_gsc_vm_thread(void) {
    int before = checkpoint();
    const char* src =
        "load()\n"
        "{\n"
        "  giveWeapon(\"kar98k_mp\");\n"
        "  wait 1;\n"
        "  giveWeapon(\"mp40_mp\");\n"
        "}\n"
        "main()\n"
        "{\n"
        "  thread load();\n"
        "  giveWeapon(\"m1garand_mp\");\n"
        "  getent(\"door\", \"targetname\");\n"
        "  giveWeapon(\"colt_mp\");\n"
        "}\n";
    GscHost host;
    memset(&host, 0, sizeof(host));
    host.give_weapon = vm_weapon_seq;
    g_vm_weapon_seq_n = 0;
    expect(gsc_vm_exec(src, strlen(src), "main", &host) == 1, "thread runs");
    expect(g_vm_weapon_seq_n == 2, "thread then caller");
    expect(g_vm_weapon_seq_n >= 1 &&
           strcmp(g_vm_weapon_seq[0], "kar98k_mp") == 0, "thread weapon first");
    expect(g_vm_weapon_seq_n >= 2 &&
           strcmp(g_vm_weapon_seq[1], "m1garand_mp") == 0, "caller weapon second");
    expect(!weapon_seq_has("mp40_mp"), "no weapon after wait");
    expect(!weapon_seq_has("colt_mp"), "no weapon after getent");
    report("test_gsc_vm_thread", before);
}

static void test_gsc_vm_cullfog(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  setCullFog(300, 3500, .32, 0, 0, 0);\n"
        "  ambientPlay(\"after_fog\");\n"
        "}\n";
    GscHost host;
    memset(&host, 0, sizeof(host));
    host.ambient = vm_ambient;
    g_vm_ambient[0] = '\0';
    expect(gsc_vm_exec(src, strlen(src), "main", &host) == 1, "fog exec");
    expect(strcmp(g_vm_ambient, "after_fog") == 0, "ambient after fog");
    report("test_gsc_vm_cullfog", before);
}

static void test_gsc_vm_funcref(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  level.callback = ::Callback_StartGameType;\n"
        "  ambientPlay(\"after_ref\");\n"
        "}\n";
    GscHost host;
    memset(&host, 0, sizeof(host));
    host.ambient = vm_ambient;
    g_vm_ambient[0] = '\0';
    expect(gsc_vm_exec(src, strlen(src), "main", &host) == 1, "ref exec");
    expect(strcmp(g_vm_ambient, "after_ref") == 0, "ambient after ref");
    report("test_gsc_vm_funcref", before);
}

static void test_gsc_vm_float(void) {
    int before = checkpoint();
    const char* src =
        "main()\n"
        "{\n"
        "  x = 0.5;\n"
        "  if (x > 0) return 3;\n"
        "  return 4;\n"
        "}\n";
    expect(gsc_vm_exec(src, strlen(src), "main", NULL) == 1, "float exec");
    expect(gsc_vm_last_int == 3, "0.5 > 0 returns 3");
    report("test_gsc_vm_float", before);
}

static void test_xmodel_bone_once(void) {
    int before = checkpoint();
    unsigned char parts[6 + 19 * 2];
    float xyz[9];
    int n;
    memset(parts, 0, sizeof(parts));
    put_u16(parts + 0, 14);
    put_u16(parts + 2, 2);
    put_u16(parts + 4, 1);
    /* bone 1, child of root 0, translation (0,0,10) */
    parts[6] = 0;
    put_f32(parts + 6 + 9, 10.f);
    /* bone 2, child of bone 1, translation (1,0,0) */
    parts[6 + 19] = 1;
    put_f32(parts + 6 + 19 + 1, 1.f);
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 3);
    expect(n == 3, "root plus two children");
    expect(xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f, "root stays at origin");
    expect(xyz[3] == 0.f && xyz[4] == 0.f && xyz[5] == 10.f, "first child");
    expect(xyz[6] == 1.f && xyz[7] == 0.f && xyz[8] == 10.f, "child adds parent once");
    /* A parent index at or past the bone is not a link. That bone stays identity. */
    parts[6 + 19] = 255;
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 3);
    expect(n == 3, "bad parent count");
    expect(xyz[6] == 0.f && xyz[7] == 0.f && xyz[8] == 0.f, "bad parent is identity");
    parts[6 + 19] = 1;
    put_u16(parts + 6 + 19 + 13, 32767);
    put_u16(parts + 6 + 19 + 15, 32767);
    put_u16(parts + 6 + 19 + 17, 32767);
    put_f32(parts + 6 + 19 + 1, 1.f);
    n = cod_xmodel_world_translations(parts, sizeof(parts), xyz, 3);
    expect(n == 3, "oversize quat count");
    expect(xyz[6] > -100.f && xyz[6] < 100.f, "oversize quat stays finite");
    report("test_xmodel_bone_once", before);
}

static void test_xmodel_bind_apply(void) {
    int before = checkpoint();
    unsigned char parts[6 + 19];
    unsigned char surf[20 + 3 * 32];
    unsigned char xm[8];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    CodXmodelBoneRule saved;
    size_t i;

    saved = cod_xmodel_bone_rule();
    memset(parts, 0, sizeof(parts));
    put_u16(parts + 0, 14);
    put_u16(parts + 2, 1);
    put_u16(parts + 4, 1);
    parts[6] = 0;
    put_f32(parts + 7, 4.f);
    put_f32(parts + 11, 5.f);
    put_f32(parts + 15, 6.f);
    cod_xmodel_set_keep_hands(0);
    put_u16(xm, 14);

    fill_one_tri(surf);
    put_u16(surf + 11, 1);
    for (i = 0; i < 3; i++) {
        put_f32(surf + 20 + i * 32 + 20, 0.f);
        put_f32(surf + 20 + i * 32 + 24, 0.f);
        put_f32(surf + 20 + i * 32 + 28, 0.f);
    }
    expect(cod_xmodel_to_obj(xm, 2, surf, sizeof(surf), parts, sizeof(parts), &obj, &sz) == 1, "rigid bone 1");
    expect(obj && strstr((const char*)obj, "v 4 5 6\n") != NULL, "world translation");
    free(obj);
    obj = NULL;

    put_u16(surf + 11, 9);
    expect(cod_xmodel_to_obj(xm, 2, surf, sizeof(surf), parts, sizeof(parts), &obj, &sz) == 1, "rigid bone 9");
    expect(obj && strstr((const char*)obj, "v 0 0 0\n") != NULL, "out of range stays origin");
    free(obj);
    obj = NULL;

    {
        unsigned char partial[20 + 3 * 32 + 1];
        fill_one_tri(partial);
        put_u16(partial + 2, 2);
        partial[sizeof(partial) - 1] = 0;
        expect(cod_xmodel_to_obj(xm, 2, partial, sizeof(partial), NULL, 0, &obj, &sz) == 1, "truncated second mesh kept");
        expect(obj && strstr((const char*)obj, "v 0 1 2\n") != NULL, "first mesh vert remains");
        free(obj);
        obj = NULL;
    }

    {
        /* Skinned triangle. Bone 1 is at (0,0,5). A single influence uses the
         * primary bone even when the additive count is zero. */
        unsigned char rig[256];
        size_t o = 0;
        unsigned v;
        memset(parts, 0, sizeof(parts));
        put_u16(parts + 0, 14);
        put_u16(parts + 2, 1);
        put_u16(parts + 4, 1);
        parts[6] = 0;
        put_f32(parts + 15, 5.f);
        memset(rig, 0, sizeof(rig));
        put_u16(rig + o, 14); o += 2;
        put_u16(rig + o, 1); o += 2;
        rig[o++] = 0;
        put_u16(rig + o, 3); o += 2;
        put_u16(rig + o, 1); o += 2;
        put_u16(rig + o, 0); o += 2;
        put_u16(rig + o, 65535); o += 2;
        o += 4;
        rig[o++] = 3;
        put_u16(rig + o, 0); o += 2;
        put_u16(rig + o, 1); o += 2;
        put_u16(rig + o, 2); o += 2;
        for (v = 0; v < 3; v++) {
            o += 12 + 8;
            put_u16(rig + o, 0); o += 2;
            put_u16(rig + o, 1); o += 2;
            if (v == 0) {
                put_f32(rig + o, 1.f); o += 4;
                o += 8;
            } else {
                o += 12;
            }
        }
        expect(cod_xmodel_to_obj(xm, 2, rig, o, parts, sizeof(parts), &obj, &sz) == 1, "rigged mesh");
        expect(obj && strstr((const char*)obj, "v 1 0 5\n") != NULL, "skinned world offset");
        free(obj);
        obj = NULL;

        /* Two influences on the same bone: 0.5*(2,0,0) + 0.5*(0,2,0) + translation. */
        o = 0;
        memset(rig, 0, sizeof(rig));
        put_u16(rig + o, 14); o += 2;
        put_u16(rig + o, 1); o += 2;
        rig[o++] = 0;
        put_u16(rig + o, 3); o += 2;
        put_u16(rig + o, 1); o += 2;
        put_u16(rig + o, 0); o += 2;
        put_u16(rig + o, 65535); o += 2;
        o += 4;
        rig[o++] = 3;
        put_u16(rig + o, 0); o += 2;
        put_u16(rig + o, 1); o += 2;
        put_u16(rig + o, 2); o += 2;
        for (v = 0; v < 3; v++) {
            o += 12 + 8;
            put_u16(rig + o, v == 0 ? 1 : 0); o += 2;
            put_u16(rig + o, 1); o += 2;
            if (v == 0) {
                put_f32(rig + o, 2.f); o += 4;
                o += 8;
                put_f32(rig + o, 0.5f); o += 4;
            } else {
                o += 12;
            }
        }
        put_u16(rig + o, 1); o += 2;
        o += 4;
        put_f32(rig + o, 2.f); o += 4;
        o += 4;
        put_f32(rig + o, 0.5f); o += 4;
        expect(cod_xmodel_to_obj(xm, 2, rig, o, parts, sizeof(parts), &obj, &sz) == 1, "blended mesh");
        expect(obj && strstr((const char*)obj, "v 1 1 5\n") != NULL, "primary plus additive");
        free(obj);
        obj = NULL;
    }

    {
        unsigned char one[20 + 3 * 32];
        unsigned char two[4 + 112 * 2];
        unsigned char skinxm[160];
        const char* name = "metal@ford.dds";
        const char* text;
        const char* u1;
        const char* u2;
        const char* v1;
        const char* v2;
        size_t n;
        fill_one_tri(one);
        memset(two, 0, sizeof(two));
        put_u16(two + 0, 14);
        put_u16(two + 2, 2);
        memcpy(two + 4, one + 4, 112);
        memcpy(two + 4 + 112, one + 4, 112);
        n = write_skin_xmodel(skinxm, sizeof(skinxm), &name, 1);
        expect(cod_xmodel_to_obj(skinxm, n, two, sizeof(two), NULL, 0, &obj, &sz) == 1, "short skin block");
        text = obj ? (const char*)obj : NULL;
        u1 = text ? strstr(text, "usemtl skins/metal@ford.dds\n") : NULL;
        v1 = text ? strstr(text, "v 0 1 2\n") : NULL;
        u2 = u1 ? strstr(u1 + 1, "usemtl skins/metal@ford.dds\n") : NULL;
        v2 = v1 ? strstr(v1 + 1, "v 0 1 2\n") : NULL;
        expect(u1 && v1 && u1 < v1, "skin before first mesh");
        expect(u2 && v2 && u2 < v2, "last skin before second mesh");
        expect(text && strstr(text, "skins/unbound.png") == NULL, "short block is not unbound");
        free(obj);
        obj = NULL;
    }

    {
        unsigned char empty[13];
        memset(empty, 0, sizeof(empty));
        put_u16(empty + 0, 14);
        put_u16(empty + 2, 1);
        put_u16(empty + 5, 3);
        put_u16(empty + 7, 1);
        expect(cod_xmodel_to_obj(xm, 2, empty, sizeof(empty), NULL, 0, &obj, &sz) == 0, "empty surf fails");
        free(obj);
        obj = NULL;
    }

    cod_xmodel_set_bone_rule(saved);
    report("test_xmodel_bind_apply", before);
}

#define HX_RETAIL_GROUPS 64
#define HX_RETAIL_BONES 256

typedef struct HxRetailGroup {
    int count;
    double sx, sy, sz;
    float minx, maxx, miny, maxy;
} HxRetailGroup;

static int retail_parse_groups(const char* obj, unsigned size, HxRetailGroup* groups, int cap) {
    const char* p = obj;
    const char* end = obj + size;
    int n = 0;
    int cur = -1;

    if (!obj) return -1;
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len >= 7 && memcmp(p, "usemtl ", 7) == 0) {
            if (n >= cap) return -1;
            cur = n++;
            groups[cur].count = 0;
            groups[cur].sx = groups[cur].sy = groups[cur].sz = 0.0;
            groups[cur].minx = groups[cur].miny = 1e30f;
            groups[cur].maxx = groups[cur].maxy = -1e30f;
        } else if (cur >= 0 && len >= 2 && p[0] == 'v' && p[1] == ' ') {
            char line[192];
            float x, y, z;
            if (len >= sizeof(line)) return -1;
            memcpy(line, p, len);
            line[len] = '\0';
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                HxRetailGroup* g = &groups[cur];
                g->count++;
                g->sx += x;
                g->sy += y;
                g->sz += z;
                if (x < g->minx) g->minx = x;
                if (x > g->maxx) g->maxx = x;
                if (y < g->miny) g->miny = y;
                if (y > g->maxy) g->maxy = y;
            }
        }
        p += len;
        if (nl) p++;
    }
    return n;
}

static int retail_helix_groups(const CodArchive* ar, const char* name, HxRetailGroup* groups, int* nout) {
    unsigned char* obj = NULL;
    unsigned sz = 0;
    int n;

    if (!xmodel_to_helix(ar, name, &obj, &sz)) {
        free(obj);
        return 0;
    }
    n = retail_parse_groups((const char*)obj, sz, groups, HX_RETAIL_GROUPS);
    free(obj);
    if (n < 0) return 0;
    *nout = n;
    return 1;
}

static int retail_within_30(double ax, double ay, double az, double bx, double by, double bz) {
    double dx = ax - bx;
    double dy = ay - by;
    double dz = az - bz;
    return dx * dx + dy * dy + dz * dz <= 30.0 * 30.0;
}

static void retail_center(const HxRetailGroup* g, double* x, double* y, double* z) {
    *x = g->sx / (double)g->count;
    *y = g->sy / (double)g->count;
    *z = g->sz / (double)g->count;
}

static int retail_largest(const HxRetailGroup* g, int n) {
    int i, best = -1, best_n = -1;
    for (i = 0; i < n; i++) {
        if (g[i].count > best_n) {
            best = i;
            best_n = g[i].count;
        }
    }
    return best;
}

static int retail_match_groups_to_bones(const HxRetailGroup* g, int ng, int largest,
                                      const float* xyz, int nbones) {
    int pick[4];
    int n = 0;
    int i, k;
    char used_group[HX_RETAIL_GROUPS];

    if (nbones <= 0 || nbones > HX_RETAIL_BONES || ng > HX_RETAIL_GROUPS) return 0;
    memset(used_group, 0, sizeof(used_group));
    for (k = 0; k < 4; k++) {
        int best = -1;
        double best_h = 0.0;
        double cx, cy, cz;
        for (i = 0; i < ng; i++) {
            double h;
            if (i == largest || used_group[i] || g[i].count <= 0) continue;
            retail_center(&g[i], &cx, &cy, &cz);
            h = cx * cx + cy * cy;
            if (best < 0 || h > best_h) {
                best = i;
                best_h = h;
            }
        }
        if (best < 0) return 0;
        used_group[best] = 1;
        pick[n++] = best;
    }
    for (k = 0; k < 4; k++) {
        double cx, cy, cz;
        int found = 0;
        retail_center(&g[pick[k]], &cx, &cy, &cz);
        for (i = 0; i < nbones; i++) {
            if (!retail_within_30(cx, cy, cz, xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2])) continue;
            found = 1;
            break;
        }
        if (!found) return 0;
    }
    return 1;
}

static int retail_peugeot_ok(const HxRetailGroup* ruled, int nr,
                             const HxRetailGroup* raw, int nraw,
                             const float* xyz, int nbones) {
    int largest, raw_i;
    double rcx, rcy, rcz, hx, hy, hz;

    largest = retail_largest(ruled, nr);
    raw_i = retail_largest(raw, nraw);
    if (largest < 0 || raw_i < 0) return 0;
    if (ruled[largest].count <= 0 || raw[raw_i].count <= 0) return 0;
    retail_center(&ruled[largest], &rcx, &rcy, &rcz);
    retail_center(&raw[raw_i], &hx, &hy, &hz);
    if (!retail_within_30(rcx, rcy, rcz, hx, hy, hz)) return 0;
    return retail_match_groups_to_bones(ruled, nr, largest, xyz, nbones);
}

static int retail_flak_ok(const HxRetailGroup* g, int n) {
    int largest = retail_largest(g, n);
    int i;
    double lx, ly, lz;

    if (largest < 0 || g[largest].count <= 0) return 0;
    retail_center(&g[largest], &lx, &ly, &lz);
    (void)lx;
    (void)ly;
    for (i = 0; i < n; i++) {
        double cx, cy, cz;
        if (i == largest || g[i].count <= 0) continue;
        retail_center(&g[i], &cx, &cy, &cz);
        if (cz > lz &&
            cx >= (double)g[largest].minx && cx <= (double)g[largest].maxx &&
            cy >= (double)g[largest].miny && cy <= (double)g[largest].maxy)
            return 1;
    }
    return 0;
}

/* Vertices under a usemtl containing tag. want_y: |y| must exceed limit.
 * Otherwise z must exceed limit. At least one matching vertex is required. */
static int retail_mat_verts(const char* obj, unsigned size, const char* tag, int want_y, float limit) {
    const char* p = obj;
    const char* end = obj + size;
    size_t ntag;
    int on = 0;
    int saw = 0;

    if (!obj || !tag) return 0;
    ntag = strlen(tag);
    while (p < end) {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len >= 7 && memcmp(p, "usemtl ", 7) == 0) {
            size_t i;
            on = 0;
            for (i = 7; i + ntag <= len; i++) {
                if (memcmp(p + i, tag, ntag) == 0) {
                    on = 1;
                    break;
                }
            }
        } else if (on && len >= 2 && p[0] == 'v' && p[1] == ' ') {
            char line[192];
            float x, y, z;
            if (len >= sizeof(line)) return 0;
            memcpy(line, p, len);
            line[len] = '\0';
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) != 3) return 0;
            (void)x;
            saw = 1;
            if (want_y) {
                if (y > -limit && y < limit) return 0;
            } else if (z <= limit) {
                return 0;
            }
        }
        p += len;
        if (nl) p++;
    }
    return saw;
}

static void test_xmodel_retail_rule(void) {
    int before = checkpoint();
    const char* env = getenv("COD_MAIN");
    const char* dir = (env && env[0]) ? env :
        "C:/Program Files (x86)/Steam/steamapps/common/Call of Duty/Main";
    DIR* opened = opendir(dir);
    CodArchive* ar = NULL;
    char pak[1024];
    unsigned char* xm = NULL;
    unsigned char* sf = NULL;
    unsigned char* peu_parts = NULL;
    unsigned char* obj = NULL;
    unsigned xsz = 0, ssz = 0, psz = 0, osz = 0;
    HxRetailGroup raw[HX_RETAIL_GROUPS];
    int nraw = 0;
    size_t dlen;

    if (!opened) {
        printf("test_xmodel_retail_rule: SKIP\n");
        return;
    }
    closedir(opened);

    ar = cod_archive_create();
    expect(ar != NULL, "retail archive");
    dlen = strlen(dir);
    if (dlen > 0 && (dir[dlen - 1] == '/' || dir[dlen - 1] == '\\'))
        snprintf(pak, sizeof(pak), "%spak0.pk3", dir);
    else
        snprintf(pak, sizeof(pak), "%s/pak0.pk3", dir);
    expect(ar && cod_archive_add_zip(ar, pak) == 1, "pak0.pk3");

    if (ar && cod_archive_read(ar, "xmodel/vehicle_peugeot_static", &xm, &xsz)) {
        CodXmodelLod slots[4];
        char surf[300];
        int nslots = cod_xmodel_lod_slots(xm, xsz, slots, 4);
        int s;
        int n = -1;
        surf[0] = '\0';
        for (s = 0; s < nslots; s++) {
            if (!slots[s].name[0]) continue;
            snprintf(surf, sizeof(surf), "xmodelsurfs/%s", slots[s].name);
            break;
        }
        if (surf[0] && cod_archive_read(ar, surf, &sf, &ssz) &&
            cod_xmodel_to_obj(xm, xsz, sf, ssz, NULL, 0, &obj, &osz))
            n = retail_parse_groups((const char*)obj, osz, raw, HX_RETAIL_GROUPS);
        if (n >= 0) nraw = n;
    }
    free(obj);
    free(sf);
    free(xm);
    obj = NULL;
    sf = NULL;
    xm = NULL;
    if (ar) cod_archive_read(ar, "xmodelparts/peugtuot1", &peu_parts, &psz);

    {
        HxRetailGroup ruled[HX_RETAIL_GROUPS];
        HxRetailGroup flak[HX_RETAIL_GROUPS];
        float xyz[HX_RETAIL_BONES * 3];
        int nr = 0, nf = 0, nbones = 0;
        if (peu_parts)
            nbones = cod_xmodel_world_translations(peu_parts, psz, xyz, HX_RETAIL_BONES);
        expect(ar && retail_helix_groups(ar, "xmodel/vehicle_peugeot_static", ruled, &nr) &&
                   retail_peugeot_ok(ruled, nr, raw, nraw, xyz, nbones),
               "peugeot bind pose");
        expect(ar && retail_helix_groups(ar, "xmodel/vehicle_tank_flakpanzer", flak, &nf) &&
                   retail_flak_ok(flak, nf),
               "flakpanzer turret");
    }

    if (ar && xmodel_to_helix(ar, "xmodel/static_vehicle_tank_tiger_snow", &obj, &osz) == 1) {
        expect(retail_mat_verts((const char*)obj, osz, "TTroadwheel", 1, 40.f),
               "tiger road wheels stay on the sides");
        free(obj);
        obj = NULL;
    } else {
        expect(0, "tiger obj");
    }
    if (ar && xmodel_to_helix(ar, "xmodel/static_vehicle_tank_PanzerIV", &obj, &osz) == 1) {
        expect(strstr((const char*)obj,"lighting runtime\n")!=NULL,"native metal lightingDiffuse is exported rather than forced baked");
        {
            unsigned char* source=NULL;unsigned source_size=0;unsigned char* baked=NULL;unsigned baked_size=0;
            const char* stype="{ /* rgbGen lightingDiffuse */ { map $texturename rgbGen lightingPrecalc } }";
            (void)cod_archive_read(ar,"shadertypes/model/metal.stype",&source,&source_size);
            expect(cod_archive_add_blob(ar,"shadertypes/model/metal.stype",stype,(unsigned)strlen(stype)),"override explicit baked shader");
            expect(xmodel_to_helix(ar,"xmodel/static_vehicle_tank_PanzerIV",&baked,&baked_size) && strstr((const char*)baked,"lighting baked\n"),"explicit precalc survives while commented generators are ignored");
            if(source)cod_archive_add_blob(ar,"shadertypes/model/metal.stype",source,source_size);
            else {const char* diffuse="{ { rgbGen lightingDiffuse } }";cod_archive_add_blob(ar,"shadertypes/model/metal.stype",diffuse,(unsigned)strlen(diffuse));}
            free(source);free(baked);
        }

        expect(retail_mat_verts((const char*)obj, osz, "wheeltredunwrap", 0, 2.f),
               "panzer wheel tread stays off the ground");
        free(obj);
        obj = NULL;
    } else {
        expect(0, "panzer obj");
    }

    if (ar && cod_archive_read(ar, "xmodel/static_vehicle_german_truck", &xm, &xsz)) {
        CodXmodelLod slots[4];
        char names[64][160];
        int nslots = cod_xmodel_lod_slots(xm, xsz, slots, 4);
        int nskins = cod_xmodel_skin_names(xm, xsz, names, 64);
        expect(nslots > 0 && strcmp(slots[0].name, "germmantruck0") == 0, "truck slot 0");
        expect(nskins == 21, "truck high lod skins");
        expect(nskins > 0 && strcmp(names[0], "metal@ford.dds") == 0, "truck first skin");
        expect(xmodel_to_helix(ar, "xmodel/static_vehicle_german_truck", &obj, &osz) == 1, "truck obj");
        expect(obj && strstr((const char*)obj, "usemtl skins/metal@ford.dds") != NULL, "truck usemtl");
        free(obj);
        obj = NULL;
    } else {
        expect(0, "truck xmodel");
    }
    free(xm);
    xm = NULL;

    {
        unsigned char* pt = NULL;
        unsigned pt_sz = 0;
        float xyz[HX_RETAIL_BONES * 3];
        int n;
        expect(ar && cod_archive_read(ar, "xmodelparts/Kubelwagen0", &pt, &pt_sz) == 1, "kubel parts");
        n = pt ? cod_xmodel_world_translations(pt, pt_sz, xyz, HX_RETAIL_BONES) : -1;
        expect(n == 1 && xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f, "kubel identity root");
        free(pt);
        pt = NULL;
        expect(ar && cod_archive_read(ar, "xmodelparts/flak88_anti-tank0", &pt, &pt_sz) == 1, "flak88 parts");
        n = pt ? cod_xmodel_world_translations(pt, pt_sz, xyz, HX_RETAIL_BONES) : -1;
        expect(n == 1 && xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f, "flak88 identity root");
        free(pt);
    }

    free(peu_parts);
    cod_archive_destroy(ar);
    report("test_xmodel_retail_rule", before);
}

/* One triangle soup. content_flags is the CoD material contents word. */
static void put_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}

static int tiny_soup_bsp(const char* mat_name, unsigned int cflags, unsigned char** out, unsigned int* out_sz) {
    const unsigned int hdr = 8u + 33u * 8u;
    const unsigned int mat_n = 72u;
    const unsigned int soup_n = 16u;
    const unsigned int vert_n = 44u * 3u;
    const unsigned int idx_n = 2u * 3u;
    unsigned int total = hdr + mat_n + soup_n + vert_n + idx_n;
    unsigned char* b;
    unsigned int mat_at, soup_at, vert_at, idx_at, i;
    if (!out || !out_sz) return 0;
    b = (unsigned char*)calloc(1, total);
    if (!b) return 0;
    b[0] = 'I'; b[1] = 'B'; b[2] = 'S'; b[3] = 'P';
    put_u32(b + 4, 59u);
    mat_at = hdr;
    soup_at = mat_at + mat_n;
    vert_at = soup_at + soup_n;
    idx_at = vert_at + vert_n;
    put_u32(b + 8 + 0 * 8, mat_n);
    put_u32(b + 8 + 0 * 8 + 4, mat_at);
    put_u32(b + 8 + 6 * 8, soup_n);
    put_u32(b + 8 + 6 * 8 + 4, soup_at);
    put_u32(b + 8 + 7 * 8, vert_n);
    put_u32(b + 8 + 7 * 8 + 4, vert_at);
    put_u32(b + 8 + 8 * 8, idx_n);
    put_u32(b + 8 + 8 * 8 + 4, idx_at);
    for (i = 0; mat_name[i] && i < 63; i++) b[mat_at + i] = (unsigned char)mat_name[i];
    put_u32(b + mat_at + 68, cflags);
    put_u32(b + soup_at + 4, 0u); /* vertex_offset */
    b[soup_at + 8] = 3;           /* vertex_count */
    b[soup_at + 10] = 3;          /* triangle_count */
    /* three positions so the triangle has area */
    put_u32(b + vert_at + 0 * 44 + 0, 0x3f800000u); /* 1 */
    put_u32(b + vert_at + 1 * 44 + 4, 0x3f800000u);
    put_u32(b + vert_at + 2 * 44 + 8, 0x3f800000u);
    b[idx_at + 2] = 1;
    b[idx_at + 4] = 2;
    *out = b;
    *out_sz = total;
    return 1;
}

static void test_fence_monsterclip_is_drawn(void) {
    int before = checkpoint();
    unsigned char* bsp = NULL;
    unsigned int bsz = 0;
    unsigned char* map = NULL;
    unsigned int msz = 0;
    const char* text;
    expect(tiny_soup_bsp("textures/normandy/transparents/metal_masked@wiremesh1", 0x20030000u, &bsp, &bsz) == 1,
           "wire bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "wire", &map, &msz) == 1, "wire hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "metal_masked@wiremesh1") != NULL, "monsterclip fence is drawn");
    expect(strstr(text, "contents playerclip") != NULL, "fence blocks players");
    free(map);
    free(bsp);
    map = NULL;
    bsp = NULL;
    expect(tiny_soup_bsp("textures/common/clip", 0x28030200u, &bsp, &bsz) == 1, "clip bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "clip", &map, &msz) == 1, "clip hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "textures/common/clip") == NULL, "clip texture stays hidden");
    free(map);
    free(bsp);
    report("test_fence_monsterclip_is_drawn", before);
}

/* Material plus one terrain triangle at z=10 and one flat 3x3 curve at z=4. */
static int tiny_terrain_bsp(unsigned int cflags, unsigned char** out, unsigned int* out_sz) {
    const unsigned int hdr = 8u + 33u * 8u;
    const unsigned int mat_n = 72u;
    const unsigned int patch_n = 16u * 2u;
    const unsigned int vert_n = 12u * (3u + 9u);
    const unsigned int idx_n = 2u * 3u;
    unsigned int total = hdr + mat_n + patch_n + vert_n + idx_n;
    unsigned char* b;
    unsigned int mat_at, patch_at, vert_at, idx_at, i;
    const char* name = "textures/normandy/ground/dirt@oldpacked_large";
    if (!out || !out_sz) return 0;
    b = (unsigned char*)calloc(1, total);
    if (!b) return 0;
    b[0] = 'I'; b[1] = 'B'; b[2] = 'S'; b[3] = 'P';
    put_u32(b + 4, 59u);
    mat_at = hdr;
    patch_at = mat_at + mat_n;
    vert_at = patch_at + patch_n;
    idx_at = vert_at + vert_n;
    put_u32(b + 8 + 0 * 8, mat_n);
    put_u32(b + 8 + 0 * 8 + 4, mat_at);
    put_u32(b + 8 + 24 * 8, patch_n);
    put_u32(b + 8 + 24 * 8 + 4, patch_at);
    put_u32(b + 8 + 25 * 8, vert_n);
    put_u32(b + 8 + 25 * 8 + 4, vert_at);
    put_u32(b + 8 + 26 * 8, idx_n);
    put_u32(b + 8 + 26 * 8 + 4, idx_at);
    for (i = 0; name[i] && i < 63; i++) b[mat_at + i] = (unsigned char)name[i];
    put_u32(b + mat_at + 68, cflags);
    /* mode 1 triangle soup: 3 verts, 3 indexes, downward winding */
    b[patch_at + 2] = 1;
    b[patch_at + 4] = 3;
    b[patch_at + 6] = 3;
    /* mode 0 curve: 3x3, maxError 8, firstVert 3 */
    b[patch_at + 16 + 4] = 3;
    b[patch_at + 16 + 6] = 3;
    put_u32(b + patch_at + 16 + 8, 8u);
    put_u32(b + patch_at + 16 + 12, 3u);
    put_f32(b + vert_at + 0, 0.f); put_f32(b + vert_at + 4, 0.f); put_f32(b + vert_at + 8, 10.f);
    put_f32(b + vert_at + 12, 0.f); put_f32(b + vert_at + 16, 8.f); put_f32(b + vert_at + 20, 10.f);
    put_f32(b + vert_at + 24, 8.f); put_f32(b + vert_at + 28, 0.f); put_f32(b + vert_at + 32, 10.f);
    b[idx_at + 2] = 1;
    b[idx_at + 4] = 2;
    for (i = 0; i < 3; i++) {
        for (int y = 0; y < 3; y++) {
            unsigned int o = vert_at + (3u + (unsigned int)(y * 3 + i)) * 12u;
            put_f32(b + o, (float)i);
            put_f32(b + o + 4, (float)y);
            put_f32(b + o + 8, 4.f);
        }
    }
    *out = b;
    *out_sz = total;
    return 1;
}

static void test_terrain_collision(void) {
    int before = checkpoint();
    unsigned char* bsp = NULL;
    unsigned int bsz = 0;
    unsigned char* map = NULL;
    unsigned int msz = 0;
    const char* text;
    expect(tiny_terrain_bsp(0x00000001u, &bsp, &bsz) == 1, "terrain bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "ground", &map, &msz) == 1, "terrain hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "textures/common/clip") != NULL, "terrain collides without a second draw");
    expect(strstr(text, "contents solid") != NULL, "terrain is solid");
    expect(strstr(text, "0.0 0.0 10.0") != NULL, "terrain triangle height");
    expect(strstr(text, "0.0 0.0 4.0") != NULL, "terrain curve height");
    free(map);
    free(bsp);
    map = NULL;
    bsp = NULL;
    expect(tiny_terrain_bsp(0u, &bsp, &bsz) == 1, "nonsolid terrain bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "ground", &map, &msz) == 1, "nonsolid terrain hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "textures/common/clip") == NULL, "nonsolid terrain is not a floor");
    free(map);
    free(bsp);
    report("test_terrain_collision", before);
}

/* Foliage volumes must not produce invisible player collision. */
static int tiny_brush_bsp(const char* mat_name, unsigned char** out, unsigned int* out_sz) {
    const unsigned int hdr = 8u + 33u * 8u;
    const unsigned int mat_n = 72u;
    const unsigned int brush_n = 4u;
    const unsigned int side_n = 48u;
    unsigned int total = hdr + mat_n + brush_n + side_n;
    unsigned char* b;
    unsigned int mat_at, brush_at, side_at, i;
    if (!out || !out_sz) return 0;
    b = (unsigned char*)calloc(1, total);
    if (!b) return 0;
    b[0] = 'I'; b[1] = 'B'; b[2] = 'S'; b[3] = 'P';
    put_u32(b + 4, 59u);
    mat_at = hdr;
    brush_at = mat_at + mat_n;
    side_at = brush_at + brush_n;
    put_u32(b + 8 + 0 * 8, mat_n);
    put_u32(b + 8 + 0 * 8 + 4, mat_at);
    put_u32(b + 8 + 3 * 8, side_n);
    put_u32(b + 8 + 3 * 8 + 4, side_at);
    put_u32(b + 8 + 4 * 8, brush_n);
    put_u32(b + 8 + 4 * 8 + 4, brush_at);
    for (i = 0; mat_name[i] && i < 63; i++) b[mat_at + i] = (unsigned char)mat_name[i];
    put_u32(b + mat_at + 68, strstr(mat_name, "clipplayer") ? 0x10000u : 1u);
    b[brush_at] = 6;
    put_f32(b + side_at + 0 * 8, 0.f);
    put_f32(b + side_at + 1 * 8, 32.f);
    put_f32(b + side_at + 2 * 8, 0.f);
    put_f32(b + side_at + 3 * 8, 32.f);
    put_f32(b + side_at + 4 * 8, 0.f);
    put_f32(b + side_at + 5 * 8, 48.f);
    *out = b;
    *out_sz = total;
    return 1;
}

static void test_foliage_clip_is_nonsolid(void) {
    int before = checkpoint();
    unsigned char* bsp = NULL;
    unsigned int bsz = 0;
    unsigned char* map = NULL;
    unsigned int msz = 0;
    const char* text;
    expect(tiny_brush_bsp("textures/common/clipfoliage", &bsp, &bsz) == 1, "foliage brush bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "bush", &map, &msz) == 1, "foliage hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "brush {") == NULL, "bush volume is not a wall");
    expect(strstr(text, "mesh {") == NULL, "no invented bush top");
    expect(strstr(text, "contents solid") == NULL, "bush is not solid");
    free(map);
    free(bsp);
    map = NULL;
    bsp = NULL;
    expect(tiny_brush_bsp("textures/common/clipplayer", &bsp, &bsz) == 1, "hedge brush bsp");
    expect(bsp && bsp_to_helix(bsp, bsz, "hedge", &map, &msz) == 1, "hedge hxmap");
    text = map ? (const char*)map : "";
    expect(strstr(text, "contents playerclip") != NULL, "straight clip stays a wall");
    free(map);
    free(bsp);
    report("test_foliage_clip_is_nonsolid", before);
}

static void test_brush_contents_and_ladder(void) {
    int before = checkpoint();
    const unsigned int flags[] = {0x28000000u, 0x28020000u, 0x2080u, 0x10000u, 0x10000u};
    int i;
    for (i = 0; i < 5; i++) {
        unsigned char *bsp = NULL, *map = NULL; unsigned int bsz = 0, msz = 0;
        expect(tiny_brush_bsp("textures/test/volume", &bsp, &bsz), "contents brush fixture");
        put_u32(bsp + 8u + 33u * 8u + 68u, flags[i]);
        if (i == 4) put_u32(bsp + 8u + 33u * 8u + 64u, 8u);
        expect(bsp_to_helix(bsp, bsz, "contents", &map, &msz), "contents conversion");
        if (i < 2) expect(strstr((const char*)map, "brush {") == NULL, "nonplayer contents do not block movement");
        else if (i == 2) expect(strstr((const char*)map, "contents shotclip") != NULL, "projectile-only clips keep their purpose");
        else if (i == 3) expect(strstr((const char*)map, "contents playerclip") != NULL, "playerclip contents survive without a name heuristic");
        else expect(strstr((const char*)map, "contents ladder") != NULL, "ladder surface flag survives import");
        free(map); free(bsp);
    }
    report("test_brush_contents_and_ladder", before);
}



static void test_initial_script_brushmodels(void) {
    int before=checkpoint();
    const char* entities="{ \"classname\" \"script_brushmodel\" \"model\" \"*1\" } "
        "{ \"classname\" \"script_brushmodel\" \"model\" \"*2\" \"targetname\" \"exploder\" } "
        "{ \"classname\" \"trigger_multiple\" \"model\" \"*3\" \"targetname\" \"minefield\" \"origin\" \"50 60 10\" }";
    unsigned int hdr=8+33*8, mat=hdr, soups=mat+72, verts=soups+64, indices=verts+528;
    unsigned int brushes=indices+24, sides=brushes+16, models=sides+192, ents=models+192;
    unsigned int total=ents+(unsigned int)strlen(entities), i,j;
    unsigned char* b=(unsigned char*)calloc(1,total); unsigned char* out=NULL; unsigned int out_size=0;
    memcpy(b,"IBSP",4); put_u32(b+4,59);
    put_u32(b+8+0*8,72);put_u32(b+12+0*8,mat);
    put_u32(b+8+3*8,192);put_u32(b+12+3*8,sides);
    put_u32(b+8+4*8,16);put_u32(b+12+4*8,brushes);
    put_u32(b+8+6*8,64);put_u32(b+12+6*8,soups);
    put_u32(b+8+7*8,528);put_u32(b+12+7*8,verts);
    put_u32(b+8+8*8,24);put_u32(b+12+8*8,indices);
    put_u32(b+8+27*8,192);put_u32(b+12+27*8,models);
    put_u32(b+8+29*8,(unsigned int)strlen(entities));put_u32(b+12+29*8,ents);
    strcpy((char*)b+mat,"textures/test/roof");put_u32(b+mat+68,1);
    memcpy(b+ents,entities,strlen(entities));
    for(i=0;i<4;++i){
        unsigned int m=models+i*48, soup=soups+i*16;
        put_u32(b+m+24,i);put_u32(b+m+28,1);put_u32(b+m+40,i);put_u32(b+m+44,1);
        b[brushes+i*4]=6;
        for(j=0;j<6;++j)put_f32(b+sides+i*48+j*8,j<2?i*100.f+(j%2)*16.f:(j%2)*16.f);
        put_u32(b+soup+4,i*3);b[soup+8]=3;b[soup+10]=3;put_u32(b+soup+12,i*3);
        for(j=0;j<3;++j){unsigned int v=verts+(i*3+j)*44;
            put_f32(b+v,i*100.f+(j==1?16.f:0.f));put_f32(b+v+4,j==2?16.f:0.f);
            put_f32(b+v+8,64.f);put_f32(b+v+36,1.f);b[indices+(i*3+j)*2]=(unsigned char)j;
        }
    }
    expect(bsp_to_helix(b,total,"initial_roofs",&out,&out_size),"submodels convert");
    expect(out && strstr((char*)out,"mins 100.0 0.0 0.0"),"intact roof collision included");
    expect(out && strstr((char*)out,"v 100.0 0.0 64.0"),"intact roof surface included");
    expect(out && strstr((char*)out,"mins 200.0") && strstr((char*)out,"v 200.0"),"destroyed replacement retained for script show");
    expect(out && strstr((char*)out,"model 2\n  visible 0\n  linked 0"),"replacement initially hidden and unlinked");
    expect(out && strstr((char*)out,"model 3\n  visible 0\n  linked 1"),"trigger submodel hidden but linked for overlap queries");
    expect(out && strstr((char*)out,"mins 350.0 60.0 10.0") && strstr((char*)out,"contents trigger"),"trigger submodel applies entity origin offset and trigger contents");
    expect(out && strstr((char*)out,"\"targetname\" \"exploder\""),"native script entity retained");
    free(out);out=NULL;memset(b+mat,0,64);strcpy((char*)b+mat,"textures/common/nodraw_notsolid");
    expect(bsp_to_helix(b,total,"trigger_tools",&out,&out_size),"tool-material trigger converts");
    expect(out && strstr((char*)out,"model 3\n  visible 0\n  linked 1") && strstr((char*)out,"contents trigger"),"native trigger entity overrides non-solid tool material and retains overlap hull");
    free(out);free(b);report("test_initial_script_brushmodels",before);
}


static void test_use_hint_menu(void) {
    const char* menu="menuDef { name Cursorhints rect 300 325 40 40 visible 1 itemDef { name icon type 8 rect 0 0 40 40 ownerdraw 72 visible 1 } }";
    unsigned char* out=NULL;unsigned int size=0;int before=checkpoint();
    expect(ui_menu_to_helix(menu,strlen(menu),&out,&size),"cursor hint menu translates");
    expect(out && strstr((char*)out,"gfx/icons/hint_usable.dds"),"native use hand icon retained");
    expect(out && strstr((char*)out,"cvarTest \"ui_use_available\""),"cursor hint conditional on availability");
    expect(out && strstr((char*)out,"showCvar \"1\""),"cursor hint visible while usable");
    expect(out && strstr((char*)out,"cvar \"ui_use_text\"") && strstr((char*)out,"use_hint_text"),"use hand has an adjacent text prompt");
    free(out);report("test_use_hint_menu",before);
}

static void test_skybox_shader_metadata(void) {
    const char* text="textures/sky/test { surfaceparm sky skyParms env/native 512 - }";
    CodShaderFile file;int before=checkpoint();
    expect(shader_read(text,strlen(text),&file),"sky shader parses");
    expect(file.count==1 && !strcmp(file.shaders[0].skybox,"env/native"),"native skyParms base retained");
    shader_note_invisible(&file);expect(!strcmp(shader_skybox("textures/sky/test"),"env/native"),"BSP sky lookup");shader_read_free(&file);
    {const char* entities="{ \"classname\" \"worldspawn\" }";unsigned int header=8+33*8,total=header+72+(unsigned)strlen(entities),size=0;unsigned char* bsp=(unsigned char*)calloc(1,total);unsigned char* out=NULL;
     memcpy(bsp,"IBSP",4);put_u32(bsp+4,59);put_u32(bsp+8,72);put_u32(bsp+12,header);memcpy(bsp+header,"textures/sky/test",17);
     put_u32(bsp+8+29*8,(unsigned)strlen(entities));put_u32(bsp+12+29*8,header+72);memcpy(bsp+header+72,entities,strlen(entities));
     expect(bsp_to_helix(bsp,total,"sky_meta",&out,&size),"sky BSP translates");expect(out && strstr((char*)out,"environment_skybox"),"sky environment retained");expect(out && strstr((char*)out,"env/native_rt") && strstr((char*)out,"env/native_dn"),"sky face dependency names exported");free(out);free(bsp);}
    report("test_skybox_shader_metadata",before);
}
static void test_native_vertex_blend_weights(void) {
    const unsigned int header=8+33*8,mat=header,soup=mat+72,verts=soup+16,indices=verts+132,total=indices+6;
    unsigned char* bsp=(unsigned char*)calloc(1,total);unsigned char* out=NULL;unsigned int size=0;int before=checkpoint(),i;
    memcpy(bsp,"IBSP",4);put_u32(bsp+4,59);
    put_u32(bsp+8,72);put_u32(bsp+12,mat);memcpy(bsp+mat,"textures/test/terrain",22);
    put_u32(bsp+8+6*8,16);put_u32(bsp+12+6*8,soup);
    bsp[soup+2]=255;bsp[soup+3]=255;bsp[soup+8]=3;bsp[soup+10]=3;
    put_u32(bsp+8+7*8,132);put_u32(bsp+12+7*8,verts);
    put_u32(bsp+8+8*8,6);put_u32(bsp+12+8*8,indices);bsp[indices+2]=1;bsp[indices+4]=2;
    for(i=0;i<3;i++){float x=i==1?64.f:0.f,y=i==2?64.f:0.f,z=1.f;
        memcpy(bsp+verts+i*44,&x,4);memcpy(bsp+verts+i*44+4,&y,4);memcpy(bsp+verts+i*44+36,&z,4);
        bsp[verts+i*44+40]=255;bsp[verts+i*44+41]=128;bsp[verts+i*44+42]=64;bsp[verts+i*44+43]=(unsigned char)(i==2?255:i*128);
    }
    expect(bsp_to_helix(bsp,total,"vertex_blend",&out,&size),"vertex blend BSP translates");
    expect(out && strstr((char*)out,"1.000000 0.501961 0.250980 0.000000"),"transparent authored vertex retained");
    expect(out && strstr((char*)out,"1.000000 0.501961 0.250980 0.501961"),"continuous authored alpha retained");
    expect(out && strstr((char*)out,"1.000000 0.501961 0.250980 1.000000"),"opaque authored vertex retained");
    free(out);free(bsp);report("test_native_vertex_blend_weights",before);
}

static void test_stance_modes_and_hud(void) {
    const char* text="menuDef {\n name options_move\n rect 0 0 400 400\n itemDef {\n name crouchmode\n type ITEM_TYPE_BIND\n cvar \"togglecrouch\"\n rect 5 220 350 13\n }\n itemDef {\n name pronemode\n type ITEM_TYPE_BIND\n cvar \"toggleprone\"\n rect 5 235 350 13\n }\n}\nmenuDef {\n name stance_hud\n itemDef {\n name stance\n ownerdraw 20\n rect 0 0 20 20\n }\n}\n";
    unsigned char* out=NULL;unsigned int size=0;int before=checkpoint();
    expect(ui_menu_to_helix(text,strlen(text),&out,&size),"stance menus translate");
    expect(out && strstr((char*)out,"cl_crouchToggle") && strstr((char*)out,"cl_proneToggle"),"independent stance controls");
    expect(out && strstr((char*)out,"\"Toggle\"") && strstr((char*)out,"\"Hold\""),"readable stance modes");
    expect(out && strstr((char*)out,"cvar \"ui_stance\"") && strstr((char*)out,"stance_prone.dds") && strstr((char*)out,"stance_crouch.dds") && strstr((char*)out,"stance_stand.dds"),"three actual stance HUD images retained");
    free(out);report("test_stance_modes_and_hud",before);
}

static void test_native_lightmap_image(void) {
    const unsigned int bytes=512*512*3,header=8+33*8;
    unsigned char* bsp=(unsigned char*)calloc(1,header+bytes);unsigned char* out=NULL;unsigned int size=0;
    hxAssetPlugin* p=helix_asset_plugin_create();FILE* file;int before=checkpoint();
    memcpy(bsp,"IBSP",4);put_u32(bsp+4,59);put_u32(bsp+16,bytes);put_u32(bsp+20,header);
    bsp[header]=10;bsp[header+1]=20;bsp[header+2]=30;
    bsp[header+bytes-3]=200;bsp[header+bytes-2]=100;bsp[header+bytes-1]=50;
    file=fopen("mp_lightmap_test.bsp","wb");expect(file!=NULL,"fixture create");if(file){fwrite(bsp,1,header+bytes,file);fclose(file);}
    expect(p && p->open(p,"mp_lightmap_test.bsp"),"fixture mount");
    expect(p && p->contains(p,"codlightmaps/mp_lightmap_test/0.tga"),"virtual lightmap exists");
    expect(p && !p->contains(p,"codlightmaps/mp_lightmap_test/1.tga"),"out of range lightmap rejected");
    expect(p && !p->contains(p,"codlightmaps/mp_lightmap_test/-1.tga"),"negative lightmap rejected");
    expect(p && p->read(p,"codlightmaps/mp_lightmap_test/0.tga",&out,&size),"native lightmap read");
    expect(size==18+bytes && out && out[17]==32,"512px top origin TGA");
    expect(out && out[18]==60 && out[19]==40 && out[20]==20,"native lighting gain and BGR order");
    expect(out && out[size-1]==255 && out[size-2]==127 && out[size-3]==63,"overbright preserves hue");
    if(p){p->free_buf(p,out);p->destroy(p);}free(bsp);remove("mp_lightmap_test.bsp");report("test_native_lightmap_image",before);
}

static void test_native_spawn_metadata(void) {
    const char* entities="{ \"classname\" \"worldspawn\" \"suncolor\" \"100 100 105\" }"
                         "{ \"classname\" \"mp_deathmatch_spawn\" \"origin\" \"10 20 100\" }"
                         "{ \"classname\" \"mp_deathmatch_intermission\" \"origin\" \"30 40 50\" }"
                         "{ \"classname\" \"misc_mg42\" \"model\" \"xmodel/mg42_bipod\" \"origin\" \"0 0 30\" }"
                         "{ \"classname\" \"script_model\" \"model\" \"xmodel/barrel_green2snowy\" \"lightingPrecalc\" \"0 0 0\" }";
    unsigned int hdr=8+33*8,total=hdr+(unsigned int)strlen(entities),n=0;
    unsigned char* bsp=(unsigned char*)calloc(1,total);unsigned char* out=NULL;int before=checkpoint();
    memcpy(bsp,"IBSP",4);put_u32(bsp+4,59);
    put_u32(bsp+8+29*8,(unsigned int)strlen(entities));put_u32(bsp+12+29*8,hdr);
    memcpy(bsp+hdr,entities,strlen(entities));
    expect(bsp_to_helix(bsp,total,"spawnmeta",&out,&n),"spawn metadata conversion");
    expect(out && strstr((char*)out,"\"classname\" \"info_player_deathmatch\""),"host spawn class retained");
    expect(out && strstr((char*)out,"\"cod_classname\" \"mp_deathmatch_spawn\""),"native spawn class retained");
    expect(out && strstr((char*)out,"\"cod_origin\" \"10 20 100\""),"native feet retained");
    expect(out && strstr((char*)out,"\"origin\" \"10.0 20.0 132.0\""),"legacy fallback centre retained before placement");
    expect(out && strstr((char*)out,"\"spawn_anchor\" \"1\""),"generic anchor tag");
    expect(out && strstr((char*)out,"\"classname\" \"mp_deathmatch_intermission\""),"intermission metadata retained");
    expect(out && strstr((char*)out,"\"render_model\" \"1\""),"native MG42 render instance retained");
    expect(out && strstr((char*)out,"\"lightingColor\" \"-1 -1 -1\""),"zero native baked sample falls back to runtime lighting");
    expect(out && strstr((char*)out,"\"suncolor\" \"0.952380955 0.952380955 1\""),"legacy sun hue normalized before host lighting");
    free(out);free(bsp);report("test_native_spawn_metadata",before);
}

static void test_opaque_window_base_stage(void) {
    int before = checkpoint(); unsigned char* out = NULL; unsigned int size = 0;
    const char* shader = "textures/austria/windows/barrackswindow3 { surfaceparm opaqueglass "
        "{ map textures/austria/windows/glass_nosight@barracksw3.jpg rgbGen identity } "
        "{ map $lightmap blendFunc filter } "
        "{ map textures/austria/windows/barrackswindow3.blend.tga blendFunc add } "
        "{ perlight map textures/austria/windows/other.tga blendFunc add } }";
    expect(shader_source_to_helix(shader, strlen(shader), &out, &size), "opaque window conversion");
    expect(out && strstr((char*)out,"albedo_map textures/austria/windows/glass_nosight@barracksw3.jpg"), "base image preserved");
    expect(out && !strstr((char*)out,"blend additive") && !strstr((char*)out,"blend alpha"), "lighting stage does not make window transparent");
    free(out); report("test_opaque_window_base_stage", before);
}


static void test_hud_and_quickmenu_bridge(void) {
    int before = checkpoint(); unsigned char* out = NULL; unsigned int size = 0;
    const char* menu = "menuDef { name quickcommands onOpen { setCvar cl_bypassMouseInput 1 } "
        "onClose { setCvar cl_bypassMouseInput 0 } execKey \"1\" { scriptMenuResponse \"1\"; close quickcommands; } "
        "itemDef { name health ownerdraw 89 rect 0 0 100 8 background \"health.dds\" } "
        "itemDef { name compass ownerdraw 84 rect 0 0 80 80 background \"compass.dds\" } }";
    expect(ui_menu_to_helix(menu, strlen(menu), &out, &size), "HUD and key menu convert");
    expect(out && strstr((char*)out,"type bar") && strstr((char*)out,"cvar \"ui_health\""), "health binds to player state");
    expect(out && strstr((char*)out,"rotationCvar \"ui_view_yaw\""), "compass retains rotation binding");
    expect(out && strstr((char*)out,"onClose") && strstr((char*)out,"execKey \"1\""), "lifecycle and key actions preserved");
    expect(out && strstr((char*)out,"menuresponse") && strstr((char*)out,"quickcommands") && !strstr((char*)out,"weapon 1"), "voice response is not mistaken for weapon selection");
    expect(out && strstr((char*)out,"set \"cl_bypassMouseInput\" \"1\""), "mixed case native setCvar recognized");
    free(out);report("test_hud_and_quickmenu_bridge", before);
}

static void test_native_game_menu_layout(void) {
    int before = checkpoint(); unsigned char* out = NULL; unsigned int size = 0;
    const char* menu = "menuDef { name weapon_german focuscolor 1 1 1 1 "
        "itemDef { name desc visible 0 origin 296 220 rect 0 0 224 64 autowrapped text \"A rifle description\" } "
        "itemDef { name stats origin 296 330 text \"Accuracy\" } "
        "itemDef { name limit type 9 rect 0 0 350 12 cvar ui_dm_scorelimit text \"Score limit\" decoration } "
        "execKey \"t\" { scriptMenuResponse \"team\"; close weapon_german; } }";
    expect(ui_menu_to_helix(menu, strlen(menu), &out, &size), "native weapon menu converts");
    expect(out && strstr((char*)out, "textWrap 1") && strstr((char*)out, "visible 0"), "wrapping and initial visibility retained");
    expect(out && strstr((char*)out, "rect 296 330 0 0"), "origin without rect retained");
    expect(out && strstr((char*)out, "type numericfield") && strstr((char*)out, "ui_dm_scorelimit"), "numeric server information retains its native field type");
    expect(out && strstr((char*)out, "menuresponse") && strstr((char*)out, "weapon_german") && !strstr((char*)out,"weapon team"), "team tab is navigation rather than a weapon");
    expect(out && strstr((char*)out, "focuscolor 1 1 1 1"), "menu focus color reaches its items");
    free(out);
    {
        const char* options="menuDef {name options itemDef {name quality type ITEM_TYPE_MULTI cvar r_picmip cvarFloatList { Low 2 Normal 1 High 0 } } itemDef {name limit type ITEM_TYPE_NUMERICFIELD cvarFloat g_timelimit 30 1 999 maxChars 3 } itemDef {name player type ITEM_TYPE_EDITFIELD cvar name maxChars 24 } itemDef {name filter type ITEM_TYPE_MULTI cvar r_textureMode cvarStrList { \"Bilinear\", \"GL_LINEAR_MIPMAP_NEAREST\", \"Trilinear\", \"GL_LINEAR_MIPMAP_LINEAR\" } } }";
        out=NULL;size=0;expect(ui_menu_to_helix(options,strlen(options),&out,&size),"native settings translate");
        expect(out && strstr((char*)out,"cvarDefault \"30\""),"native cvarFloat default is retained");
        expect(out && strstr((char*)out,"type multi") && strstr((char*)out,"choices { \"2\" \"Low\" \"1\" \"Normal\" \"0\" \"High\""),"native discrete labels and values retained");
        expect(out && strstr((char*)out,"GL_LINEAR_MIPMAP_NEAREST") && strstr((char*)out,"Trilinear"),"comma separated native string choices retained");
        expect(out && strstr((char*)out,"type numericfield") && strstr((char*)out,"numericRange 1") && strstr((char*)out,"maxChars 3") && strstr((char*)out,"type editfield"),"numeric range and editable text retained");free(out);
    }
 report("test_native_game_menu_layout", before);
}

static void test_sound_csv_native_preamble(void) {
    int before = checkpoint(); SoundAliasFile aliases;
    const char* csv = "\"# comment with \"\"quoted\"\" words\",,\r\n,name,description\r\n"
        "name,file,vol_min,channel,loop\r\nambient_test,ambient/night.wav,0.5,local,looping\r\n";
    expect(sound_read_aliases(csv, strlen(csv), &aliases), "native CSV preamble skipped");
    expect(aliases.count == 1 && strcmp(aliases.aliases[0].name,"ambient_test") == 0, "ambient alias survives native comments");
    sound_read_free(&aliases);report("test_sound_csv_native_preamble", before);
}

static int pc_count, pc_kinds[8], pc_layout;
static char pc_names[8][96];
static void test_pc(void* user, int kind, const char* name) {
    (void)user;
    if (pc_count < 8) { pc_kinds[pc_count] = kind; snprintf(pc_names[pc_count], 96, "%s", name); }
    ++pc_count;
}
static void test_pc_cvar(void* user, const char* key, const char* value) {
    (void)user; pc_layout = strcmp(key, "scr_layoutimage") == 0 && strcmp(value, "layout") == 0;
}
static void test_gsc_precache_host(void) {
    int before = checkpoint(); GscHost host;
    const char* source = "main(){ n = \"tree\"; precacheModel(n); precacheSound(\"voice\"); precacheShader(\"gfx/hud/foo\"); precacheItem(\"kar98k_mp\"); fx = loadFx(\"explosions/test\"); setcvar(\"scr_layoutimage\",\"layout\"); return fx == \"explosions/test\"; }";
    memset(&host, 0, sizeof(host)); host.precache = test_pc; host.set_cvar = test_pc_cvar;
    pc_count = pc_layout = 0;
    expect(gsc_vm_exec(source, strlen(source), "main", &host) == 1, "precache VM executes");
    expect(pc_count == 5, "all five dependency types reach the host");
    expect(pc_kinds[0] == GSC_PC_MODEL && strcmp(pc_names[0], "tree") == 0, "computed model argument preserved");
    expect(pc_kinds[1] == GSC_PC_SOUND && pc_kinds[2] == GSC_PC_SHADER && pc_kinds[3] == GSC_PC_WEAPON && pc_kinds[4] == GSC_PC_FX, "dependency kinds preserved");
    expect(pc_layout == 1, "layout cvar reaches host");
    expect(gsc_vm_last_int == 1, "loadFx returns a reusable script handle");
    report("test_gsc_precache_host", before);
}


static void test_native_dependency_descriptors(void) {
    int before=checkpoint(); const char* file="hxpc_dependencies_test.pk3";
    const char* weapon="WEAPONFILE\\gunModel\\viewmodel_test\\worldModel\\xmodel/world_test\\fireSound\\test_fire\\viewFlashEffect\\fx/test.efx\\idleAnim\\idle_test\\altWeapon\\other_mp";
    const char* effect="Particle { shaders [ gfx/a \"gfx/b\" ] models [ debris ] sounds [ test_fire ] effects [ test ] }";
    ZipItem items[2];hxAssetPlugin* plugin;unsigned char* text=NULL;unsigned size=0;
    items[0].name="weapons/mp/test_mp";items[0].data=weapon;items[0].size=(unsigned)strlen(weapon);
    items[1].name="fx/test.efx";items[1].data=effect;items[1].size=(unsigned)strlen(effect);
    expect(write_stored_pk3(file,items,2)==1,"dependency fixture archive");
    plugin=helix_asset_plugin_create();expect(plugin&&plugin->open(plugin,file),"dependency plugin open");
    if (plugin) {
        expect(plugin->contains(plugin,"weapons/mp/test_mp.hxprecache"),"virtual weapon descriptor exists");
        expect(plugin->read(plugin,"weapons/mp/test_mp.hxprecache",&text,&size),"weapon descriptor translated");
        expect(text&&strstr((char*)text,"model \"xmodel/viewmodel_test\"")&&strstr((char*)text,"sound \"test_fire\"")&&strstr((char*)text,"data \"xanim/idle_test\""),"weapon dependencies typed and normalized");
        if(text)plugin->free_buf(plugin,text);text=NULL;
        expect(plugin->read(plugin,"fx/test.efx.hxprecache",&text,&size),"FX descriptor translated");
        expect(text&&strstr((char*)text,"shader \"gfx/a\"")&&strstr((char*)text,"shader \"gfx/b\"")&&strstr((char*)text,"fx \"fx/test.efx\""),"FX lists and quoted names normalized");
        if(text)plugin->free_buf(plugin,text);
        expect(!plugin->contains(plugin,"fx/missing.efx.hxprecache"),"missing descriptor is absent");
        plugin->destroy(plugin);
    }
    remove(file);report("test_native_dependency_descriptors",before);
}

static int rt_test_fails,rt_test_visible=1,rt_test_linked=1,rt_test_reads;
#define RT_CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);rt_test_fails++;}}while(0)
static void rt_test_error(void* u,const char* s){(void)u;printf("VM: %s\n",s);}
static int rt_test_find(void* u,const char* k,const char* v,int* ids,int cap){(void)u;if(strcmp(k,"targetname") || strcmp(v,"door"))return 0;if(cap)ids[0]=7;return 1;}
static const char* rt_test_key(void* u,int id,const char* k){(void)u;return id==7 && !strcmp(k,"targetname")?"door":id==7 && !strcmp(k,"classname")?"script_model":"";}
static int rt_test_state(void* u,int id,int v,int l){(void)u;if(id!=7)return 0;if(v>=0)rt_test_visible=v;if(l>=0)rt_test_linked=l;return 1;}
static int rt_test_readfile(void* u,const char* p,char** text,size_t* size){const char* s="pause(){wait 0.1; level.far=9;}";(void)u;rt_test_reads++;if(strcmp(p,"lib.gsc"))return 0;*size=strlen(s);*text=malloc(*size+1);strcpy(*text,s);return 1;}
static int rt_test_field(GscRuntime* r,const char* k){int n=-999;gsc_runtime_level_int(r,k,&n);return n;}
static GscRuntime* rt_test_runtime(const char* s){GscHost h={0};GscRuntime* r;h.error=rt_test_error;h.entity_find=rt_test_find;h.entity_key=rt_test_key;h.entity_state=rt_test_state;h.read_file=rt_test_readfile;r=gsc_runtime_create(&h);RT_CHECK(r!=NULL);RT_CHECK(gsc_runtime_start(r,"test.gsc",s,strlen(s),"main",-1));return r;}
static void test_gsc_runtime(void){GscRuntime* r;GscArgument arg={1,42,0,NULL};int before;
 r=rt_test_runtime("main(){level.n=1; nested(); level.n=level.n+1;} nested(){wait 0.1;level.n=level.n+2;}");
 gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_field(r,"n")==1);RT_CHECK(gsc_runtime_threads(r)==1);
 gsc_runtime_tick(r,99,10000);RT_CHECK(rt_test_field(r,"n")==1);gsc_runtime_tick(r,100,10000);RT_CHECK(rt_test_field(r,"n")==4);RT_CHECK(gsc_runtime_threads(r)==0);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){thread listen();level.notify(\"go\",42);wait 0;level.done=1;} listen(){level waittill(\"go\",n);level.payload=n;}");
 gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_field(r,"done")==-999);gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_field(r,"done")==1);RT_CHECK(rt_test_field(r,"payload")==42);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){level endon(\"stop\");deep();level.bad=1;}deep(){level waittill(\"go\",n);level.payload=n;wait 1;level.bad=2;}");
 gsc_runtime_tick(r,0,10000);RT_CHECK(gsc_runtime_notify(r,-1,"go",&arg,1)==1);gsc_runtime_tick(r,1,10000);RT_CHECK(rt_test_field(r,"payload")==42);gsc_runtime_notify(r,-1,"stop",NULL,0);gsc_runtime_tick(r,2000,10000);RT_CHECK(rt_test_field(r,"bad")==-999);RT_CHECK(!gsc_runtime_threads(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){e=getent(\"door\",\"targetname\");e thread change();wait 0.2;level.count=getentarray(\"door\",\"targetname\").size;}change(){self hide();wait 0.1;self unlinkFromWorld();self show();}");
 gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_visible==0 && rt_test_linked==1);gsc_runtime_tick(r,100,10000);RT_CHECK(rt_test_visible==1 && rt_test_linked==0);gsc_runtime_tick(r,200,10000);RT_CHECK(rt_test_field(r,"count")==1);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){lib::pause();level.after=1;}");before=rt_test_reads;gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_field(r,"after")==-999);gsc_runtime_tick(r,100,10000);RT_CHECK(rt_test_field(r,"far")==9 && rt_test_field(r,"after")==1);RT_CHECK(rt_test_reads==before);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){while(1){level.n=1;}}");RT_CHECK(gsc_runtime_tick(r,0,20)<=20);RT_CHECK(gsc_runtime_threads(r)==1);gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){thread a();thread b();}a(){level endon(\"both\");level waittill(\"both\");level.bad=1;}b(){level waittill(\"both\",n);level.good=n;}");
 gsc_runtime_tick(r,0,10000);RT_CHECK(gsc_runtime_notify(r,-1,"both",&arg,1)==1);gsc_runtime_tick(r,0,10000);RT_CHECK(rt_test_field(r,"bad")==-999 && rt_test_field(r,"good")==42);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){e=getent(\"door\");e nested();self.x=8;level.restored=self.x;}nested(){wait 0;self.x=3;level.entity=self.x;}");
 gsc_runtime_tick(r,0,10000);gsc_runtime_tick(r,1,10000);RT_CHECK(rt_test_field(r,"entity")==3 && rt_test_field(r,"restored")==8);RT_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){thread spin();thread done();}spin(){while(1){level.spin=1;}}done(){level.fair=7;}");
 for(before=0;before<8;before++)RT_CHECK(gsc_runtime_tick(r,before,100)<=100);
 RT_CHECK(rt_test_field(r,"fair")==7);RT_CHECK(gsc_runtime_threads(r)==1);gsc_runtime_destroy(r);
 r=rt_test_runtime("main(){e=getent(\"door\");e.saved=42;for(i=0;i<20000;i++){a=getentarray(\"door\");o=spawnstruct();o.cycle=o;wait 0;}level.saved=getent(\"door\").saved;level.n=i;}");
 for(before=0;before<20001;before++)gsc_runtime_tick(r,before,1000);
 RT_CHECK(rt_test_field(r,"n")==20000 && rt_test_field(r,"saved")==42);RT_CHECK(!gsc_runtime_errors(r));RT_CHECK(!gsc_runtime_threads(r));gsc_runtime_destroy(r);
 printf("rt_test_runtime tests: %d failures\n",rt_test_fails);expect(rt_test_fails==0,"persistent GSC runtime");}

#undef RT_CHECK

static int cb_test_errors,cb_test_visible=1;
#define CB_CHECK(x) do{if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);cb_test_errors++;}}while(0)
static void cb_test_err(void* u,const char* s){(void)u;printf("VM %s\n",s);}
static int cb_test_find(void* u,const char* key,const char* val,int* ids,int cap){(void)u;(void)key;if(!strcmp(val,"door") && cap){ids[0]=7;return 1;}return 0;}
static int cb_test_state(void* u,int id,int v,int l){(void)u;(void)l;if(id!=7)return 0;if(v>=0)cb_test_visible=v;return 1;}
static int cb_test_readfile(void* u,const char* p,char** text,size_t* size){const char* s=NULL;(void)u;
 if(!strcmp(p,"maps/mp/gametypes/_callbacksetup.gsc"))s="CodeCallback_StartGameType(){if(!isDefined(level.gametypestarted) || !level.gametypestarted){[[level.callbackStartGameType]]();level.gametypestarted=true;}}";
 if(!strcmp(p,"library.gsc"))s="start(n){wait 0.1;level.far=n;}";
 if(!s)return 0;*size=strlen(s);*text=malloc(*size+1);strcpy(*text,s);return 1;}
static GscRuntime* cb_test_make(const char* s){GscHost h={0};GscRuntime* r;h.error=cb_test_err;h.read_file=cb_test_readfile;h.entity_find=cb_test_find;h.entity_state=cb_test_state;r=gsc_runtime_create(&h);CB_CHECK(gsc_runtime_start(r,"test.gsc",s,strlen(s),"main",-1));return r;}
static int cb_test_value(GscRuntime* r,const char* s){int n=-1;gsc_runtime_level_int(r,s,&n);return n;}
static void test_gsc_start_callbacks(void){GscRuntime* r;
 r=cb_test_make("main(){level.callbackStartGameType=::start;level.count=0;} start(){level.count=level.count+1;e=getent(\"door\");e hide();wait 2;e show();}");
 CB_CHECK(gsc_runtime_start_gametype(r));CB_CHECK(gsc_runtime_start_gametype(r));gsc_runtime_tick(r,0,200000);CB_CHECK(cb_test_value(r,"count")==1 && cb_test_visible==0);
 gsc_runtime_tick(r,1999,10000);CB_CHECK(cb_test_visible==0);gsc_runtime_tick(r,2000,10000);CB_CHECK(cb_test_visible==1 && cb_test_value(r,"gametypestarted")==1);
 CB_CHECK(gsc_runtime_start_gametype(r));gsc_runtime_tick(r,3000,10000);CB_CHECK(cb_test_value(r,"count")==1);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){level.callbackStartGameType=::start;level.ready=0;wait 0.5;level.ready=1;}start(){level.seen=level.ready;}");CB_CHECK(gsc_runtime_start_gametype(r));gsc_runtime_tick(r,0,10000);CB_CHECK(cb_test_value(r,"seen")==0);gsc_runtime_tick(r,500,10000);CB_CHECK(cb_test_value(r,"seen")==0 && cb_test_value(r,"ready")==1);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){f=library::start;[[f]](42);level.after=1;}");gsc_runtime_tick(r,0,10000);CB_CHECK(cb_test_value(r,"after")==-1);gsc_runtime_tick(r,100,10000);CB_CHECK(cb_test_value(r,"far")==42 && cb_test_value(r,"after")==1);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){f=::check;e=getent(\"door\");e thread [[f]](8);wait 0;level.after=1;}check(n){self.x=n;wait 0;level.self=self.x;}");gsc_runtime_tick(r,0,10000);gsc_runtime_tick(r,1,10000);CB_CHECK(cb_test_value(r,"self")==8 && cb_test_value(r,"after")==1);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){level.calls=0;a=true || side();b=false && side();c=false || true && !false;level.ok=c;}side(){level.calls=level.calls+1;return 1;}");gsc_runtime_tick(r,0,10000);CB_CHECK(cb_test_value(r,"calls")==0 && cb_test_value(r,"ok")==1);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){level.sum=4+parent();}parent(){thread child();return 6;}child(){wait 1;}");gsc_runtime_tick(r,0,10000);CB_CHECK(cb_test_value(r,"sum")==10);CB_CHECK(!gsc_runtime_errors(r));gsc_runtime_destroy(r);
 r=cb_test_make("main(){f=42;[[f]]();level.bad=1;}");gsc_runtime_tick(r,0,10000);CB_CHECK(cb_test_value(r,"bad")==-1);CB_CHECK(gsc_runtime_errors(r)==1);CB_CHECK(!gsc_runtime_threads(r));gsc_runtime_destroy(r);
 printf("callback tests: %d failures\n",cb_test_errors);expect(cb_test_errors==0,"GSC startup callbacks");}

#undef CB_CHECK
static void test_native_weapon_catalog(void){
    int before=checkpoint();CodArchive* ar=cod_archive_create();unsigned char* output=NULL;unsigned size=0,a=0,b=0;
    const char* raw="WEAPONFILE\\damage\\68\\fireTime\\0.12\\clipSize\\30\\startAmmo\\180\\weaponType\\bullet\\weaponClass\\rifle\\gunModel\\viewmodel_mp44\\idleAnim\\viewmodel_mp44_idle";
    cod_archive_add_blob(ar,"weapons/mp/mp44_mp",raw,(unsigned)strlen(raw));
    {const char* grenade="WEAPONFILE\\weaponType\\grenade\\weaponClass\\grenade\\worldModel\\xmodel/weapon_grenade\\projectileModel\\xmodel/projectile_grenade";
     const char* rocket="WEAPONFILE\\weaponType\\projectile\\weaponClass\\rocketlauncher";
     cod_archive_add_blob(ar,"weapons/mp/fraggrenade_mp",grenade,(unsigned)strlen(grenade));
     cod_archive_add_blob(ar,"weapons/mp/panzerfaust_mp",rocket,(unsigned)strlen(rocket));}
    expect(cod_weapon_catalog(ar,&output,&size),"native catalog exported");
    expect(output && strstr((char*)output,"name \"mp44_mp\"") && strstr((char*)output,"damage 68") && strstr((char*)output,"startAmmo 180"),"retail fields preserved without heuristic rows");
    expect(output && strstr((char*)output,"projectile \"xmodel/projectile_grenade\"") && !strstr((char*)output,"xmodel/xmodel/"),"native model prefixes occur exactly once");
    expect(output && strstr((char*)output,"disposable 1"),"native single-use launcher metadata exported");free(output);
    expect(cod_weapon_view_exists(ar,"cod/viewhands/mp44_mp/4/profile.txt"),"valid weapon view path");
    expect(!cod_weapon_view_exists(ar,"cod/viewhands/../4/profile.txt") && !cod_weapon_view_exists(ar,"cod/viewhands/mp44_mp/14/hands.obj"),"invalid variant rejected");
    expect(cod_weapon_view_checksum(ar,"cod/viewhands/mp44_mp/4/profile.txt",&a),"view dependency checksum");
    cod_archive_add_blob(ar,"xanim/viewmodel_mp44_idle","change",6);
    expect(cod_weapon_view_checksum(ar,"cod/viewhands/mp44_mp/4/profile.txt",&b)&&a!=b,"animation replacement invalidates converted view cache");
    cod_archive_destroy(ar);report("test_native_weapon_catalog",before);
}

static void test_gsc_lexical_boundaries(void) {
    int before = checkpoint();
    const char* source =
        "/* fake(){return 99;} */\n"
        "main(){ /* comment with } and // */ return helper(); }\n"
        "helper(){s=\"} http://host/a /* literal */\"; // }\n"
        "return 37; }";
    GscRuntime* r;
    expect(gsc_vm_exec(source, strlen(source), "main", NULL) == 1,
           "comments and string braces preserve function boundaries");
    expect(gsc_vm_last_int == 37, "helper after quoted brace executes");
    r = cb_test_make("main(){level.url=\"http://host/a\";level.ok=1;}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "ok") == 1 && !gsc_runtime_errors(r),
           "line comment markers inside string preserve following statements");
    gsc_runtime_destroy(r);
    r = cb_test_make("main(){level.le=3<=3;level.ge=4>=3;level.ne=2!=3;"
                     "level.order=0==2<1;level.loop=0;"
                     "for(i=0;i<=3;i++)level.loop=level.loop+1;"
                     "level.falsele=4<=3;level.falsene=2!=2;}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "le") == 1 && cb_test_value(r, "ge") == 1 &&
           cb_test_value(r, "ne") == 1 && cb_test_value(r, "order") == 1 &&
           cb_test_value(r, "loop") == 4 && cb_test_value(r, "falsele") == 0 &&
           cb_test_value(r, "falsene") == 0 && !gsc_runtime_errors(r),
           "comparison operators, precedence and inclusive loop bound");
    gsc_runtime_destroy(r);
    expect(!gsc_vm_exec("main(){return 1;} /* unterminated",
                       strlen("main(){return 1;} /* unterminated"), "main", NULL),
           "unterminated block comment rejects source");
    report("test_gsc_lexical_boundaries", before);
}

static void test_gsc_localized_value(void) {
    int before = checkpoint();
    GscRuntime* r = cb_test_make("main(){level.token=&\"MENU_TEST\";"
        "level.same=level.token==&\"MENU_TEST\";level.other=level.token!=&\"MENU_OTHER\";"
        "level.typed=level.token!=\"MENU_TEST\";thread child(level.token);}"
        "child(value){wait 0;level.child=value==&\"MENU_TEST\";}");
    gsc_runtime_tick(r, 0, 10000);
    gsc_runtime_tick(r, 1, 10000);
    expect(cb_test_value(r, "same") == 1 && cb_test_value(r, "other") == 1 &&
           cb_test_value(r, "typed") == 1 && cb_test_value(r, "child") == 1 &&
           !gsc_runtime_errors(r), "localized keys retain type across fields, threads and waits");
    gsc_runtime_destroy(r);
    r = cb_test_make("main(){a=\"prefix\"+4;level.concat=a==\"prefix4\";"
        "level.numeric=(2+\".\"+5)==\"2.5\";"
        "level.escape=\"say \\\"hi\\\"\"==\"say \"+\"\\\"hi\\\"\";"
        "level.path=\"maps\\mp\"==\"maps\"+\"\\mp\";}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "concat") == 1 && cb_test_value(r, "numeric") == 1 &&
           cb_test_value(r, "escape") == 1 && cb_test_value(r, "path") == 1 &&
           !gsc_runtime_errors(r), "string concatenation, numeric formatting and escaped quotes");
    gsc_runtime_destroy(r);
    report("test_gsc_localized_value", before);
}

static void test_gsc_vector_values(void) {
    int before = checkpoint();
    GscRuntime* r = cb_test_make("main(){a=(1,2,3);b=(4,5,6);"
        "level.add=(a+b)==(5,7,9);level.sub=(b-a)==(3,3,3);"
        "level.mul=(a*2)==(2,4,6);level.left=(2*a)==(2,4,6);"
        "level.div=(a/2)==(.5,1,1.5);level.neg=(-a)==(-1,-2,-3);"
        "level.index=a[1]==2;level.literal=(7,8,9)[2]==9;"
        "thread child(a);}child(v){wait 0;level.child=v==(1,2,3);}");
    gsc_runtime_tick(r, 0, 10000);
    gsc_runtime_tick(r, 1, 10000);
    expect(cb_test_value(r,"add")==1 && cb_test_value(r,"sub")==1 &&
           cb_test_value(r,"mul")==1 && cb_test_value(r,"left")==1 &&
           cb_test_value(r,"div")==1 && cb_test_value(r,"neg")==1 &&
           cb_test_value(r,"index")==1 && cb_test_value(r,"literal")==1 &&
           cb_test_value(r,"child")==1 && !gsc_runtime_errors(r),
           "vector arithmetic, indexing and value lifetime across waits");
    gsc_runtime_destroy(r);
    report("test_gsc_vector_values", before);
}

static void test_gsc_associative_arrays(void) {
    int before = checkpoint();
    GscRuntime* r = cb_test_make("main(){a=[];a[9]=7;a[\"9\"]=12;"
        "level.count=a.size;level.numeric=a[9];level.string=a[\"9\"];"
        "a[9]=undefined;level.deleted=a.size;"
        "a[\"9\"]=undefined;level.empty=a.size;"
        "a[\"spark\"]=spawnstruct();a[\"spark\"].value=42;level.effects=a;"
        "for(i=0;i<4100;i++)junk=[];wait 0;"
        "level.gc=level.effects[\"spark\"].value;}");
    gsc_runtime_tick(r, 0, 200000);
    gsc_runtime_tick(r, 1, 10000);
    expect(cb_test_value(r,"count")==2, "mixed sparse array counts defined keys");
    expect(cb_test_value(r,"numeric")==7 && cb_test_value(r,"string")==12,
           "numeric and string array keys remain distinct");
    expect(cb_test_value(r,"deleted")==1 && cb_test_value(r,"empty")==0,
           "undefined assignment deletes array entries");
    expect(cb_test_value(r,"gc")==42, "associative values remain GC roots");
    expect(!gsc_runtime_errors(r), "associative array execution has no diagnostics");
    gsc_runtime_destroy(r);
    r = cb_test_make("main(){a=[];a[\"token\"]=42;library::start(a);"
                     "level.imported=level.far[\"token\"];}");
    gsc_runtime_tick(r, 0, 10000);
    gsc_runtime_tick(r, 100, 10000);
    expect(cb_test_value(r,"imported")==42 && !gsc_runtime_errors(r),
           "associative values cross imported call and wait");
    gsc_runtime_destroy(r);
    report("test_gsc_associative_arrays", before);
}

static void test_gsc_paths_and_receiver_expressions(void) {
    int before = checkpoint();
    GscRuntime* r = cb_test_make("main(){level.data=[];level.data[\"obj\"]=spawnstruct();"
        "level.data[\"obj\"].value=4;level.data[\"obj\"].value+=2;"
        "level.data[\"obj\"].value++;level.value=level.data[\"obj\"].value;"
        "level.test=!(level.data[\"obj\"] check(8));"
        "level.data[\"obj\"] change(9);level.changed=level.data[\"obj\"].value;"
        "level.bits=(4|3)^2;level.flag=!(4&2);level.mask=4&6;"
        "level.fx[\"smoke\"]=13;level.auto=level.fx[\"smoke\"];"
        "a[1][\"key\"]=17;level.localauto=a[1][\"key\"]; }"
        "check(n){return self.value==n;}change(n){self.value=n;}");
    gsc_runtime_tick(r, 0, 200000);
    expect(cb_test_value(r,"value")==7 && cb_test_value(r,"test")==1 &&
           cb_test_value(r,"changed")==9 && cb_test_value(r,"bits")==5 &&
           cb_test_value(r,"flag")==1 && cb_test_value(r,"mask")==4 &&
           cb_test_value(r,"auto")==13 && cb_test_value(r,"localauto")==17 &&
           !gsc_runtime_errors(r), "nested assignment, receiver expressions and bitwise masks");
    gsc_runtime_destroy(r);
    report("test_gsc_paths_and_receiver_expressions", before);
}

static int frontend_reads;
static int frontend_read(void *user, const char *path, char **out, size_t *size) {
    const char *source = NULL;
    (void)user;
    frontend_reads++;
    if (!strcmp(path, "inc/math.gsc"))
        source = "#include inc\\common; Add(n){wait 0;return Twice(n)+1;} Set(n){self.value=n;}";
    if (!strcmp(path, "inc/common.gsc"))
        source = "Twice(n){return n*2;}";
    if (!strcmp(path, "inc/other.gsc"))
        source = "Other(){return 11;}";
    if (!source)
        return 0;
    *size = strlen(source);
    *out = (char *)malloc(*size + 1);
    if (!*out)
        return 0;
    memcpy(*out, source, *size + 1);
    return 1;
}
static void test_gsc_strict_frontend(void) {
    int before = checkpoint(), i;
    GscProg p;
    GscCompileDiagnostic error;
    GscRuntime *r;
    GscHost host = {0};
    const char *invalid[] = {"main(){return 1}",
                             "main(){x=(1+);}",
                             "main(){foo(1,);}",
                             "main(){break;}",
                             "main(){continue;}",
                             "main(){x=1e+;}",
                             "main(){x=1abc;}",
                             "main(){nonsense;}",
                             "main(){x=[];x[0]=;}",
                             "main(){if(1){}else}",
                             "main(){switch(1){case 1:break;case 1:break;}}",
                             "main(){switch(1){default:;default:;}}",
                             "main(){x=\"unfinished;}",
                             "main(){/* unfinished}",
                             "main(a,a){}",
                             "main(){} MAIN(){}", "if(){}", "main(level){}", "main(){x=else;}"};
    for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); i++) {
        expect(!gsc_compile_runtime_options(invalid[i], strlen(invalid[i]), &p, NULL, &error),
               "invalid GSC rejects the entire program");
        expect(p.ncode == 0 && p.nfunc == 0 && p.code == NULL && error.message[0],
               "failed GSC emits no executable partial program");
    }
    {
        const char *source = "main(){\n return (1+);\n}";
        expect(!gsc_compile_runtime_options(source, strlen(source), &p, NULL, &error) &&
                   error.line == 2 && error.column == 12,
               "syntax diagnostic identifies exact offending token");
    }
    {
        char *source = (char *)malloc(20000);
        size_t at = 0;
        for (i = 0; i < 300; i++)
            at += (size_t)sprintf(source + at, "func%d(){return %d;}\n", i, i);
        expect(gsc_compile_runtime_options(source, at, &p, NULL, &error) && p.nfunc == 300,
               "strict compiler retains all functions beyond former 64-function truncation");
        gsc_prog_free(&p);
        free(source);
    }
    r = cb_test_make(
        "main(){i=0;s=0;do{i++;if(i==2)continue;s+=i;}while(i<4);"
        "switch(i){case 4:s+=10;case 5:s+=2;break;default:s=0;}level.sum=s;"
        "level.cast=(int)7.9;level.float=((float)3/2)==1.5;level.mod=17%5;"
        "level.left=3<<4;level.right=-8>>2;level.complement=~0;"
        "level.once=0;a=[];a[0]=2;a[index()]+=5;level.update=a[0];"
        "f=::worker;level.ref=[[f]](8); }index(){level.once++;return 0;}worker(string){return string+1;}");
    gsc_runtime_tick(r, 0, 200000);
    expect(
        cb_test_value(r, "sum") == 20 && cb_test_value(r, "cast") == 7 &&
            cb_test_value(r, "float") == 1 && cb_test_value(r, "mod") == 2 &&
            cb_test_value(r, "left") == 48 && cb_test_value(r, "right") == -2 &&
            cb_test_value(r, "complement") == -1 && cb_test_value(r, "once") == 1 &&
            cb_test_value(r, "update") == 7 && cb_test_value(r, "ref") == 9 &&
            !gsc_runtime_errors(r),
        "strict bytecode executes loops, casts, shifts and single-evaluation compound references");
    gsc_runtime_destroy(r);
    host.read_file = frontend_read;
    host.error = cb_test_err;
    frontend_reads = 0;
    r = gsc_runtime_create(&host);
    {
        const char *source =
            "#include inc\\math; #include inc/other; Main(){level.result=ADD(20);"
            "obj=spawnstruct();obj SET(7);level.method=obj.value;f=::Other;level.ref=[[f]]();"
            "thread done();}done(){level.done=1;}";
        expect(gsc_runtime_start(r, "test.gsc", source, strlen(source), "MAIN", -1),
               "included functions link before execution");
    }
    i = frontend_reads;
    gsc_runtime_tick(r, 0, 200000);
    gsc_runtime_tick(r, 1, 200000);
    expect(cb_test_value(r, "result") == 41 && cb_test_value(r, "method") == 7 &&
               cb_test_value(r, "ref") == 11 && cb_test_value(r, "done") == 1 &&
               !gsc_runtime_errors(r),
           "transitive includes, receiver calls, references and identifier case execute correctly");
    expect(frontend_reads == i, "included calls perform no runtime source reads");
    gsc_runtime_destroy(r);
    r = gsc_runtime_create(&host);
    {
        const char *source = "#include inc\\missing; main(){level.mustnotrun=1;}";
        expect(!gsc_runtime_start(r, "missing.gsc", source, strlen(source), "main", -1),
               "missing mandatory include rejects entrypoint");
        gsc_runtime_tick(r, 0, 10000);
        expect(cb_test_value(r, "mustnotrun") == -1, "invalid include cannot run partial root");
    }
    gsc_runtime_destroy(r);
    report("test_gsc_strict_frontend", before);
}

static void test_gsc_frontend_events_and_developer(void) {
    int before = checkpoint();
    GscRuntime *r;
    r = cb_test_make(
        "main(){o=spawnstruct();o thread listen();wait 0;"
        "o notify(\"event\",6,\"go\");wait 0;level.early=isDefined(level.matched);"
        "o notify(\"event\",7,\"go\");wait 0;level.done=1;}"
        "listen(){self endon(\"stop\");self waittillmatch(\"event\",7,\"go\");level.matched=1;}");
    gsc_runtime_tick(r, 0, 200000);
    gsc_runtime_tick(r, 1, 200000);
    expect(cb_test_value(r, "matched") == -1, "waittillmatch ignores a mismatched payload");
    gsc_runtime_tick(r, 2, 200000);
    gsc_runtime_tick(r, 3, 200000);
    expect(cb_test_value(r, "early") == 0 && cb_test_value(r, "matched") == 1 &&
               cb_test_value(r, "done") == 1 && !gsc_runtime_errors(r),
           "struct receiver waittillmatch resumes on matching payload");
    gsc_runtime_destroy(r);
    r = cb_test_make("/# hidden(){level.dev=1;} #/ main(){hidden();"
                     "/# level.inline=1; #/ level.done=1;}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "dev") == -1 && cb_test_value(r, "inline") == -1 &&
               cb_test_value(r, "done") == 1 && !gsc_runtime_errors(r),
           "disabled developer blocks parse but do not execute");
    gsc_runtime_destroy(r);
    r = cb_test_make("#using_animtree(\"tree_a\"); one(){return %idle;}"
                     "#using_animtree(\"tree_b\");two(){return %idle;}main(){"
                     "level.distinct=one()!=two();level.same=%idle==%idle;level.other=%idle!=%walk;"
                     "anim.flag=8;level.global=anim.flag;}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "distinct") == 1 && cb_test_value(r, "same") == 1 &&
               cb_test_value(r, "other") == 1 && cb_test_value(r, "global") == 8 &&
               !gsc_runtime_errors(r),
           "animation references preserve per-function tree and anim global");
    gsc_runtime_destroy(r);
    r = cb_test_make(
        "main(){level.phase=1;waittillframeend;level.phase=2;waittillframeend;level.phase=3;}");
    gsc_runtime_tick(r, 0, 10000);
    expect(cb_test_value(r, "phase") == 2,
           "frame-end wait resumes after normal work in the same tick");
    gsc_runtime_tick(r, 1, 10000);
    expect(cb_test_value(r, "phase") == 3 && !gsc_runtime_errors(r),
           "repeated frame-end wait resumes once per tick");
    gsc_runtime_destroy(r);
    report("test_gsc_frontend_events_and_developer", before);
}


static void test_declarative_ui_defaults(void) {
    const char* cfg="bind f +activate\nexec malicious.cfg\nset scr_sd_roundlength 4\nset sv_hostname \"Existing server\"\nset invalid-key 1\nset broken \"unterminated\n";
    unsigned char* output=NULL;unsigned int size=0;
    expect(ui_cvar_defaults_from_cfg(cfg,strlen(cfg),&output,&size),"export declarative cfg defaults");
    if(output){
        expect(strstr((char*)output,"\"scr_sd_roundlength\" \"4\"")!=NULL,"native gametype value survives");
        expect(strstr((char*)output,"\"sv_hostname\" \"Existing server\"")!=NULL,"quoted default value survives");
        expect(!strstr((char*)output,"activate") && !strstr((char*)output,"malicious") && !strstr((char*)output,"invalid-key") && !strstr((char*)output,"broken"),"commands and malformed entries are not executable defaults");
        free(output);output=NULL;
    }
    {
        const char* menu="menuDef {name fixture itemDef {type ITEM_TYPE_TEXT text Fixture} onOpen {execOnCvarIntValue g_allowvote 0 \"set shown 0\";execOnCvarStringValue g_gametype sd \"set mode 1\";} }";
        expect(ui_menu_to_helix(menu,strlen(menu),&output,&size),"conditional native menu parses");
        if(output){expect(strstr((char*)output,"exec_if_int") && strstr((char*)output,"exec_if_string") && strstr((char*)output,"set shown 0") && strstr((char*)output,"set mode 1"),"conditional comparisons retain their command payload");free(output);}
    }
}


static void test_gametype_catalog(void) {
    CodArchive* ar=cod_archive_create();unsigned char* out=NULL;unsigned size=0;
    const char* menu="menuDef { name createserver itemDef { type ITEM_TYPE_MULTI ownerdraw UI_NETGAMETYPE text Game rect 0 0 300 20 } }";
    const char* loc="REFERENCE TEST_MODE\nLANG_ENGLISH \"Patched mode\"\n";
    ui_loc_add_str("CATALOG",loc,strlen(loc));
    cod_archive_add_blob(ar,"maps/mp/gametypes/_teams.gsc","main(){}",8);
    cod_archive_add_blob(ar,"maps/mp/gametypes/custom.gsc","main(){}",8);
    cod_archive_add_blob(ar,"maps/mp/gametypes/custom.txt","\"CATALOG_TEST_MODE\"",19);
    cod_archive_add_blob(ar,"maps/mp/gametypes/bare.gsc","main(){}",8);
    cod_archive_add_blob(ar,"maps/mp/gametypes/nested/helper.gsc","main(){}",8);
    cod_archive_add_blob(ar,"maps/mp/gametypes/orphan.txt","orphan",6);
    ui_menu_set_gametype_archive(ar);
    expect(ui_menu_to_helix(menu,strlen(menu),&out,&size),"archive gametype menu translates");
    expect(out && strstr((char*)out,"cvar \"g_gametype\""),"gametype selector is bound");
    expect(out && strstr((char*)out,"\"custom\" \"Patched mode\""),"gametype localized title");
    expect(out && strstr((char*)out,"\"bare\" \"bare\""),"missing title falls back to script id");
    expect(out && !strstr((char*)out,"_teams") && !strstr((char*)out,"helper") && !strstr((char*)out,"orphan"),"shared and nested scripts and orphan title excluded");free(out);out=NULL;
    cod_archive_add_blob(ar,"maps/mp/gametypes/custom.txt","\"Replacement title\"",19);
    expect(ui_menu_to_helix(menu,strlen(menu),&out,&size),"catalog rebuild after mount override");
    expect(out && strstr((char*)out,"Replacement title") && !strstr((char*)out,"Patched mode"),"final archive override wins");free(out);
    ui_menu_set_gametype_archive(NULL);cod_archive_destroy(ar);
}


static void test_gsc_player_callbacks(void) {
    const char* script="main(){level.callbackPlayerConnect=::connect;level.callbackPlayerDamage=::damage;}"
        "connect(){self.value=7;self endon(\"disconnect\");self waittill(\"begin\");level.began=self.value;self waittill(\"menuresponse\",menu,response);level.menu=menu;level.response=response;wait .05;level.finished=self.value;}"
        "damage(attacker,amount,point){self.value+=amount;level.vector=point[2];level.attacker=attacker;level.damage=amount;}";
    GscRuntime* r=cb_test_make(script);GscArgument args[3]={{0}},read={0};char response[32]="axis";
    gsc_runtime_tick(r,0,20000);
    expect(gsc_runtime_call_level(r,"callbackPlayerConnect",-3,NULL,0),"dispatch actual connect function reference");
    gsc_runtime_tick(r,0,20000);expect(gsc_runtime_get_entity_value(r,-3,"value",NULL,&read) && read.integer==7,"callback persistent self");
    expect(gsc_runtime_notify(r,-3,"begin",NULL,0)==1,"begin wakes connect waiter");gsc_runtime_tick(r,0,20000);
    expect(cb_test_value(r,"began")==7,"begin delivered after connect subscribes");
    args[0].kind=3;args[0].text="team_menu";args[1].kind=3;args[1].text=response;
    expect(gsc_runtime_notify(r,-3,"menuresponse",args,2)==1,"menu event wakes player callback");strcpy(response,"changed");gsc_runtime_tick(r,0,20000);
    expect(gsc_runtime_get_entity_value(r,-1,"response",NULL,&read) && read.kind==3 && !strcmp(read.text,"axis"),"notification copies borrowed payload");
    args[0].kind=4;args[0].integer=-4;args[1].kind=1;args[1].integer=9;args[2].kind=5;args[2].vector[2]=42;
    expect(gsc_runtime_call_level(r,"callbackPlayerDamage",-3,args,3),"typed player damage dispatch");gsc_runtime_tick(r,0,20000);
    expect(cb_test_value(r,"damage")==9 && gsc_runtime_get_entity_value(r,-1,"vector",NULL,&read) && read.kind==2 && read.number==42,"typed damage and vector payload execute");
    gsc_runtime_tick(r,60,20000);expect(cb_test_value(r,"finished")==16,"callback wait preserves updated player self");
    expect(gsc_runtime_get_entity_value(r,-1,"attacker",NULL,&read) && read.kind==4 && read.integer==-4,"entity payload identity preserved");
    expect(!gsc_runtime_call_level(r,"missingCallback",-3,NULL,0),"missing callback is not invented");
    expect(gsc_runtime_call_level(r,"callbackPlayerConnect",-3,NULL,0),"second subscription starts");gsc_runtime_tick(r,60,20000);
    gsc_runtime_forget_entity(r,-3);expect(gsc_runtime_threads(r)==0,"disconnect retires waiting player threads");
    expect(!gsc_runtime_get_entity_value(r,-3,"value",NULL,&read),"reused client entity has fresh script fields");
    args[0].kind=1;args[0].integer=3;expect(gsc_runtime_set_entity_value(r,-2,"round",NULL,&args[0]),"persistent round set");
    args[0].kind=3;args[0].text="rifle_mp";expect(gsc_runtime_set_entity_value(r,-3,"pers","weapon",&args[0]),"persistent weapon set");
    expect(gsc_runtime_reset_map(r),"map restart resets scheduler");
    expect(gsc_runtime_get_entity_value(r,-2,"round",NULL,&read) && read.integer==3,"round state survives restart");
    expect(gsc_runtime_get_entity_value(r,-3,"pers","weapon",&read) && read.kind==3 && !strcmp(read.text,"rifle_mp"),"player pers survives restart");
    expect(!gsc_runtime_get_entity_value(r,-1,"began",NULL,&read),"level state does not survive restart");
    expect(gsc_runtime_threads(r)==0,"restart cancels every old waiter");
    expect(gsc_runtime_start(r,"test.gsc",script,strlen(script),"main",-1),"cached script main restarts");gsc_runtime_tick(r,0,20000);
    expect(gsc_runtime_call_level(r,"callbackPlayerConnect",-3,NULL,0),"callbacks re-register on new level");gsc_runtime_tick(r,0,20000);
    expect(gsc_runtime_notify(r,-3,"begin",NULL,0)==1,"new level waiter receives begin exactly once");gsc_runtime_tick(r,0,20000);
    expect(gsc_runtime_errors(r)==0,"lifecycle callbacks have no VM diagnostics");gsc_runtime_destroy(r);
}


static int lifecycle_alias_count;
static int lifecycle_alias_exists(void* u,const char* name){(void)u;return !strcmp(name,"known_alias");}
static void lifecycle_precache(void* u,int kind,const char* name){(void)u;if(kind==GSC_PC_SOUND && !strcmp(name,"known_alias"))lifecycle_alias_count++;else expect(0,"unknown literal must not be guessed as sound");}
static void test_gsc_lifecycle_semantics(void){
    {
        GscArgument vector={0},read={0};GscRuntime* event_runtime=rt_test_runtime("main(){level waittill(\"vector\",point);level.vector=point[2];}");
        gsc_runtime_tick(event_runtime,0,20000);vector.kind=5;vector.vector[2]=42;
        expect(gsc_runtime_notify(event_runtime,-1,"vector",&vector,1)==1,"native vector notification wakes waiter");gsc_runtime_tick(event_runtime,0,20000);
        expect(gsc_runtime_get_entity_value(event_runtime,-1,"vector",NULL,&read) && read.kind==2 && read.number==42,"native notification preserves vector payload");gsc_runtime_destroy(event_runtime);
    }

    GscRuntime* r=rt_test_runtime("main(){level endon(\"stop\");level notify(\"stop\");level.value=1;wait .05;level.value=2;}");
    gsc_runtime_tick(r,0,20000);expect(rt_test_field(r,"value")==1,"self notify does not terminate current executing stack");
    gsc_runtime_notify(r,-1,"stop",NULL,0);gsc_runtime_tick(r,50,20000);
    expect(rt_test_field(r,"value")==2 && !gsc_runtime_errors(r),"self-notified endon subscription is removed before later suspension");gsc_runtime_destroy(r);
    r=rt_test_runtime("main(){e=getent(\"door\");e.targetname=\"renamed\";level.found=getent(\"renamed\")==e;level.old=isdefined(getent(\"door\"));a=getentarray(\"renamed\");level.count=a.size;}");
    gsc_runtime_tick(r,0,20000);expect(rt_test_field(r,"found")==1 && rt_test_field(r,"old")==0 && rt_test_field(r,"count")==1,"queries respect runtime property overrides without duplicate native entities");expect(!gsc_runtime_errors(r),"dynamic entity query diagnostics");gsc_runtime_destroy(r);
    r=rt_test_runtime("main(){level.valid=1;for(i=0;i<1000;i++){n=randomint(4.8);if(n<0 || n>3)level.valid=0;}}");
    gsc_runtime_tick(r,0,200000);expect(rt_test_field(r,"valid")==1 && !gsc_runtime_errors(r),"stock float-valued spawn scores convert safely for randomInt");gsc_runtime_destroy(r);
    r=cb_test_make("main(){level.callbackStartGameType=::start;i=0;while(i<200)i++;wait .1;level.later=1;}start(){level.started=1;}");
    expect(gsc_runtime_start_gametype(r),"startup wrapper request");gsc_runtime_tick(r,0,10);
    expect(!gsc_runtime_start_dispatched(r),"budget preemption is not a main yield");gsc_runtime_tick(r,0,200000);
    expect(cb_test_value(r,"started")==1 && gsc_runtime_start_complete(r),"explicit main wait allows startup before main continuation");
    expect(cb_test_value(r,"later")==-1,"main wait remains suspended during loading");gsc_runtime_tick(r,100,20000);expect(cb_test_value(r,"later")==1,"main continuation resumes after startup");gsc_runtime_destroy(r);
    {
        GscHost host={0};const char* source="main(){x=\"not_alias\";}unused(){alias=\"known_alias\";self playLocalSound(alias);}";
        host.precache=lifecycle_precache;host.sound_alias_exists=lifecycle_alias_exists;r=gsc_runtime_create(&host);lifecycle_alias_count=0;
        expect(gsc_runtime_start(r,"dependencies.gsc",source,strlen(source),"main",-1),"compile retained alias dependencies");
        gsc_runtime_precache_dependencies(r);expect(lifecycle_alias_count==1,"alias-indexed variable sound dependency retained");
        expect(gsc_runtime_reset_map(r),"restart compiled program for dependency registration");gsc_runtime_precache_dependencies(r);
        expect(lifecycle_alias_count==2,"cached programs re-register map-owned sounds on restart");gsc_runtime_destroy(r);
    }
    {
        GscRuntime* r_solid = rt_test_runtime("main(){e=getent(\"door\");e notsolid();level.unsolid=1;e solid();level.solid=1;}");
        gsc_runtime_tick(r_solid, 0, 20000);
        expect(rt_test_field(r_solid, "unsolid") == 1 && rt_test_field(r_solid, "solid") == 1, "entity solid/notsolid toggle succeeds");
        expect(!gsc_runtime_errors(r_solid), "solid method executes without runtime error");
        gsc_runtime_destroy(r_solid);
    }
}

static void test_generic_native_animation(void){
    const unsigned char input[]={14,0,2,0,1,0,3,30,0,1,0,0,0,2,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,160,65,0,0,0,0,0,0,0,0,0,0,99,104,105,108,100,0,1,0,0,0,0,0,0,0,1,0,0,0,0,64,0,0,0,0,0,0,0,0};
    unsigned char* output=NULL;unsigned size=0;float distance=0;
    if(!cod_xanim_generic(input,sizeof(input),&output,&size) || size!=232 || memcmp(output,"HXAC1",5)){
        fprintf(stderr,"FAIL: compressed delta-motion stream export\n");exit(1);
    }
    memcpy(&distance,output+24,4);
    if(distance!=20 || strcmp((char*)output+36,"child")){fprintf(stderr,"FAIL: native delta and channel identity\n");exit(1);}
    free(output);output=NULL;
    {unsigned char marked[sizeof(input)+8];memcpy(marked,input,sizeof(input));marked[sizeof(input)]=1;memcpy(marked+sizeof(input)+1,"step",5);marked[sizeof(input)+6]=1;marked[sizeof(input)+7]=0;
     expect(cod_xanim_generic(marked,sizeof(marked),&output,&size)&&size==300,"native note table and implicit end exported");
     expect(output && !strcmp((char*)output+168,"step") && !strcmp((char*)output+236,"end"),"native note names retained");free(output);output=NULL;
     expect(!cod_xanim_generic(marked,sizeof(marked)-1,&output,&size),"truncated note key rejected");}
    if(cod_xanim_generic(input,sizeof(input)-1,&output,&size)){free(output);fprintf(stderr,"FAIL: truncated native pose accepted\n");exit(1);}
}
static void test_animated_shader_material(void) {
    const char* source="textures/test/flow { { map textures/test/flow.dds tcMod scroll .125 -.25 tcMod rotate 45 tcMod scale 2 3 tcMod turb 0 .03 .5 .25 } }";
    unsigned char* text=NULL; unsigned size=0;
    expect(shader_source_to_helix(source,strlen(source),&text,&size),"animated shader converts");
    expect(text && strstr((char*)text,"uvScroll 0.125 -0.25"),"scroll parameters exported");
    expect(text && strstr((char*)text,"uvRotate 45"),"rotation exported");
    expect(text && strstr((char*)text,"uvTransform 2 0 0 3 0 0"),"scale uses native transform order");
    expect(text && strstr((char*)text,"uvTurb 0 "),"turbulence exported");
    free(text);
}

static void test_footstep_alias_variants(void){
    int before=checkpoint();unsigned char* out=NULL;unsigned int size=0;
    const char* csv="name,sequence,file,vol_min,vol_max,pitch_min,pitch_max,dist_min,dist_max,channel\n"
      "step_run_wood,1,footsteps/wood1.wav,.2,.5,.9,1.1,50,1000,body\n"
      "step_run_wood,2,footsteps/wood2.wav,.2,.5,.9,1.1,50,1000,body\n";
    expect(sound_csv_to_helix(csv,strlen(csv),&out,&size),"step CSV translates");
    expect(out && strstr((char*)out,"step_run_wood__variant_1") && strstr((char*)out,"step_run_wood__variant_2"),"all step samples addressable");
    expect(out && strstr((char*)out,"spatial 1") && strstr((char*)out,"max_dist 1000"),"step distance retained");
    free(out);out=NULL;
    csv="name,sequence,file,vol_min,vol_max,pitch_min,pitch_max,dist_min,dist_max,channel\n"
        "german_follow_me,1,voice/follow.wav,.8,1,1,1,80,5000,voice\n";
    expect(sound_csv_to_helix(csv,strlen(csv),&out,&size),"spoken voice CSV translates");
    expect(out && strstr((char*)out,"spatial 1") && strstr((char*)out,"max_dist 900"),"spoken radio uses bounded world attenuation");
    free(out);report("test_footstep_alias_variants",before);
}
static void test_vote_menu_conversion(void) {
    int before=checkpoint();unsigned char* output=NULL;unsigned int size=0;
    const char* menu="{ menuDef { name callvote onOpen {uiScript loadArenas;} "
        "itemDef {name vote type ITEM_TYPE_BUTTON visible 1 cvarTest allow disableCvar {\"0\"} action {uiScript voteMap;}} "
        "itemDef {name type visible 1 ownerdraw UI_NETGAMETYPE} "
        "itemDef {name players type ITEM_TYPE_LISTBOX visible 1 feeder FEEDER_PLAYER_LIST} "
        "itemDef {name kick type ITEM_TYPE_BUTTON visible 1 action {uiScript voteKick;}} "
        "itemDef {name ban type ITEM_TYPE_BUTTON visible 1 action {uiScript voteTempBan;}} "
        "itemDef {name mode type ITEM_TYPE_BUTTON visible 1 action {uiScript voteTypeMap;}} }}";
    expect(ui_menu_to_helix(menu,strlen(menu),&output,&size),"stock vote operations convert");
    if(output){const char* text=(const char*)output;
        expect(strstr(text,"stackGroup cod_session")!=NULL,"session tabs share exclusive group");
        expect(strstr(text,"ui_vote map") && strstr(text,"ui_vote typemap") && strstr(text,"ui_vote clientkick") && strstr(text,"ui_vote tempbanuser"),"every stock vote submission reaches host command");
        expect(strstr(text,"ui_vote_player") && strstr(text,"players"),"kick list uses stable player-id feeder");
        expect(strstr(text,"ui_vote_gametype")!=NULL,"vote gametype selection cannot change live server rules");
        expect(strstr(text,"disableCvar")!=NULL,"vote permissions preserve disabled choices");
        free(output);
    }
    report("test_vote_menu_conversion",before);
}

int main(void) {
    test_footstep_alias_variants();
    test_animated_shader_material();
    test_generic_native_animation();
    test_declarative_ui_defaults();
    test_gsc_frontend_events_and_developer();
    test_gsc_strict_frontend();
    test_gsc_paths_and_receiver_expressions();
    test_gsc_associative_arrays();
    test_gsc_vector_values();
    test_gsc_localized_value();
    test_gsc_lexical_boundaries();
    test_hud_and_quickmenu_bridge();
    test_native_game_menu_layout();
    test_sound_csv_native_preamble();
    test_plugin_create();
    test_archive_blob();
    test_bsp_names_and_header();
    test_fence_monsterclip_is_drawn();
    test_terrain_collision();
    test_foliage_clip_is_nonsolid();
    test_brush_contents_and_ladder();
    test_xmodel_through_archive();
    test_xmodel_v14_lod();
    test_view_basis_rigid();
    test_view_tag_from_parts();
    test_viewhand_text();
    test_viewhand_idle_pose();
    test_viewhand_pose_overlay();
    test_viewhand_idle_retail();
    test_xmodel_lod_slots();
    test_xmodel_first_lod();
    test_xmodel_v14_surf();
    test_xmodel_skin_order();
    test_xmodel_bone_once();
    test_xmodel_bind_apply();
    test_xmodel_retail_rule();
    test_sound_gameplay_alias();
    test_xanim();
    test_sound_ui_gsc();
    test_texture_shader_menu();
    test_opaque_window_base_stage();
    test_initial_script_brushmodels();
    test_native_spawn_metadata();
    test_skybox_shader_metadata();
    test_native_vertex_blend_weights();
    test_stance_modes_and_hud();
    test_native_lightmap_image();
    test_gametype_catalog();
    test_use_hint_menu();
    test_plugin_bsp_open();
    test_cvar_menu_and_cache();
    test_gsc_lifecycle_semantics();
    test_gsc_player_callbacks();
    test_gsc_start_callbacks();
    test_gsc_runtime();
    test_vote_menu_conversion();
    test_gsc_vm();
    test_gsc_vm_fib();
    test_gsc_vm_array();
    test_gsc_vm_far();
    test_gsc_vm_thread();
    test_gsc_vm_cullfog();
    test_gsc_vm_funcref();
    test_gsc_vm_float();
    test_gsc_precache_host();
    test_native_dependency_descriptors();
    test_native_weapon_catalog();
    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("hxfs_cod_test: PASS\n");
    return 0;
}
