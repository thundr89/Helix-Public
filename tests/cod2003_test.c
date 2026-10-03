/*
 * Unit tests for cod2003 asset plugin & CoD1 IBSP v59 parser
 * Copyright (C) 2026 Helix-Public contributors
 */

#include "iasset_plugin.h"
#include "cod_bsp.h"
#include "cod_xmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Forward declaration of plugin create */
hxAssetPlugin* helix_asset_plugin_create(void);

static void test_plugin_interface(void) {
    hxAssetPlugin* p = helix_asset_plugin_create();
    assert(p != NULL);
    assert(p->api_version == 2);
    assert(strcmp(p->format_name, "cod2003") == 0);
    assert(p->open != NULL);
    assert(p->open_dir != NULL);
    assert(p->contains != NULL);
    assert(p->read != NULL);
    assert(p->list != NULL);
    assert(p->close != NULL);
    assert(p->destroy != NULL);

    p->close(p);
    p->destroy(p);
    printf("test_plugin_interface: PASS\n");
}

static void put_u16(unsigned char* p, unsigned short v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void put_f32(unsigned char* p, float v) {
    unsigned int u;
    memcpy(&u, &v, 4);
    put_u32(p, u);
}

static void set_lump(unsigned char* bsp, int index, unsigned int length, unsigned int offset) {
    unsigned char* p = bsp + 8 + index * 8;
    put_u32(p, length);
    put_u32(p + 4, offset);
}

static void test_bsp_parser(void) {
    /* Entities live in lump 29. Spawn Z is lifted by 32 for the player hull. */
    const char* ent_text =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"mp_deathmatch_spawn\"\n\"origin\" \"64 128 32\"\n\"angle\" \"180\"\n}\n"
        "{\n\"classname\" \"misc_model\"\n\"model\" \"xmodel/lamp_post\"\n"
        "\"origin\" \"10 20 30\"\n\"angles\" \"0 90 0\"\n\"modelscale\" \"1.5\"\n}\n"
        "{\n\"classname\" \"misc_model\"\n\"model\" \"*0\"\n\"origin\" \"0 0 0\"\n}\n";
    unsigned int ent_len = (unsigned int)strlen(ent_text);
    unsigned int header = 8 + 33 * 8;
    unsigned int ent_off = header;
    unsigned int bsp_size = ent_off + ent_len;
    unsigned char* bsp = (unsigned char*)calloc(1, bsp_size);
    unsigned char* out_buf = NULL;
    unsigned int out_sz = 0;

    assert(bsp != NULL);
    bsp[0] = 'I'; bsp[1] = 'B'; bsp[2] = 'S'; bsp[3] = 'P';
    put_u32(bsp + 4, 59);
    set_lump(bsp, 29, ent_len, ent_off);
    memcpy(bsp + ent_off, ent_text, ent_len);

    assert(cod_bsp_is_valid(bsp, bsp_size) == 1);
    assert(cod_bsp_to_hxmap(bsp, bsp_size, "mp_test", &out_buf, &out_sz) == 1);
    assert(out_buf != NULL);
    assert(strstr((const char*)out_buf, "map {") != NULL);
    assert(strstr((const char*)out_buf, "name \"mp_test\"") != NULL);
    assert(strstr((const char*)out_buf, "classname \"info_player_deathmatch\"") != NULL);
    assert(strstr((const char*)out_buf, "origin \"64.0 128.0 64.0\"") != NULL);
    assert(strstr((const char*)out_buf, "classname \"misc_model\"") != NULL);
    assert(strstr((const char*)out_buf, "model \"xmodel/lamp_post\"") != NULL);
    assert(strstr((const char*)out_buf, "angles \"0 90 0\"") != NULL);
    assert(strstr((const char*)out_buf, "modelscale \"1.5\"") != NULL);
    assert(strstr((const char*)out_buf, "\"*0\"") == NULL);
    assert(strstr((const char*)out_buf, "brush {") != NULL);

    free(out_buf);
    free(bsp);
    printf("test_bsp_parser: PASS\n");
}

static void test_xmodel_text(void) {
    const char* text =
        "MODEL\n"
        "VERSION 5\n"
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
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    assert(cod_xmodel_is_text(text, strlen(text)) == 1);
    assert(cod_xmodel_to_obj(text, strlen(text), NULL, 0, NULL, 0, &obj, &sz) == 1);
    assert(obj != NULL);
    assert(strstr((const char*)obj, "v 1 2 3\n") != NULL);
    assert(strstr((const char*)obj, "f 1 2 3\n") != NULL);
    free(obj);
    printf("test_xmodel_text: PASS\n");
}

static void test_xmodel_binary(void) {
    unsigned char header[64];
    unsigned char surfs[256];
    size_t hoff = 0, soff = 0;
    char lod[64];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    int i;

    memset(header, 0, sizeof(header));
    memset(surfs, 0, sizeof(surfs));
    put_u16(header + hoff, 5); hoff += 2;
    put_u32(header + hoff, 0); hoff += 4;
    memcpy(header + hoff, "tree", 5); hoff += 5; /* "tree\0" */
    for (i = 0; i < 3; i++) {
        put_u32(header + hoff, 0); hoff += 4;
        header[hoff++] = 0; /* empty LOD name */
    }
    assert(cod_xmodel_lod_name(header, hoff, lod, sizeof(lod)) == 1);
    assert(strcmp(lod, "tree") == 0);

    /* One rigid triangle. numVerts == numVerts2, so it is not skinned. */
    put_u16(surfs + soff, 5); soff += 2;
    put_u16(surfs + soff, 1); soff += 2; /* numMeshes */
    surfs[soff++] = 0;                   /* flags */
    put_u16(surfs + soff, 0); soff += 2; /* unk */
    put_u16(surfs + soff, 3); soff += 2; /* numVerts */
    put_u16(surfs + soff, 1); soff += 2; /* numTris */
    put_u16(surfs + soff, 3); soff += 2; /* numVerts2 */
    put_u32(surfs + soff, 0); soff += 4; /* rigid bone */
    for (i = 0; i < 3; i++) {
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 1); soff += 4; /* normal */
        soff += 4;                           /* color */
        put_f32(surfs + soff, 0.5f); soff += 4;
        put_f32(surfs + soff, 0.25f); soff += 4; /* uv */
        soff += 24;                          /* pad */
        put_f32(surfs + soff, (float)i); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4; /* pos */
    }
    put_u16(surfs + soff, 0); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    put_u16(surfs + soff, 2); soff += 2;
    assert(soff < sizeof(surfs));

    assert(cod_xmodel_to_obj(header, hoff, surfs, soff, NULL, 0, &obj, &sz) == 1);
    assert(strstr((const char*)obj, "v 0 0 0\n") != NULL);
    assert(strstr((const char*)obj, "v 1 0 0\n") != NULL);
    assert(strstr((const char*)obj, "v 2 0 0\n") != NULL);
    /* Indices are rewound: 0,2,1 -> OBJ 1 3 2. */
    assert(strstr((const char*)obj, "f 1/1/1 3/3/3 2/2/2\n") != NULL);
    free(obj);
    printf("test_xmodel_binary: PASS\n");
}

static void test_xmodel_v14(void) {
    unsigned char header[64];
    unsigned char surfs[256];
    size_t hoff = 0, soff = 0;
    char lod[64];
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    int i;

    memset(header, 0, sizeof(header));
    memset(surfs, 0, sizeof(surfs));
    put_u16(header + hoff, 14); hoff += 2;
    hoff += 24; /* mins/maxs */
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "crate0", 7); hoff += 7;
    assert(cod_xmodel_lod_name(header, hoff, lod, sizeof(lod)) == 1);
    assert(strcmp(lod, "crate0") == 0);

    /* One rigid triangle, triangle-strip count byte, bone != 65535. */
    put_u16(surfs + soff, 14); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    surfs[soff++] = 0;
    put_u16(surfs + soff, 3); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    put_u16(surfs + soff, 0); soff += 2;
    put_u16(surfs + soff, 0); soff += 2; /* rigid bone */
    surfs[soff++] = 3;                   /* strip length */
    put_u16(surfs + soff, 0); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    put_u16(surfs + soff, 2); soff += 2;
    for (i = 0; i < 3; i++) {
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 1); soff += 4;
        put_f32(surfs + soff, 0.5f); soff += 4;
        put_f32(surfs + soff, 0.25f); soff += 4;
        put_f32(surfs + soff, (float)i); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
    }
    assert(soff < sizeof(surfs));
    assert(cod_xmodel_to_obj(header, hoff, surfs, soff, NULL, 0, &obj, &sz) == 1);
    assert(strstr((const char*)obj, "v 0 0 0\n") != NULL);
    assert(strstr((const char*)obj, "v 1 0 0\n") != NULL);
    assert(strstr((const char*)obj, "v 2 0 0\n") != NULL);
    assert(strstr((const char*)obj, "f 1/1/1 3/3/3 2/2/2\n") != NULL);
    free(obj);
    printf("test_xmodel_v14: PASS\n");
}

static void test_xmodel_v14_bones(void) {
    unsigned char header[64];
    unsigned char surfs[256];
    unsigned char parts[64];
    size_t hoff = 0, soff = 0, poff = 0;
    unsigned char* obj = NULL;
    unsigned int sz = 0;
    int i;

    memset(header, 0, sizeof(header));
    memset(surfs, 0, sizeof(surfs));
    memset(parts, 0, sizeof(parts));

    put_u16(header + hoff, 14); hoff += 2;
    hoff += 24; /* mins/maxs */
    put_f32(header + hoff, 0.f); hoff += 4;
    memcpy(header + hoff, "truck_wheel0", 13); hoff += 13;

    /* xmodelparts: ver=14, num_bones=1, unk=1 */
    put_u16(parts + poff, 14); poff += 2;
    put_u16(parts + poff, 1); poff += 2;
    put_u16(parts + poff, 1); poff += 2;
    /* Bone 0: 19 bytes. byte 0 = parent(0), bytes 1..12 = tx, ty, tz */
    parts[poff++] = 0;
    put_f32(parts + poff, 10.0f); poff += 4;
    put_f32(parts + poff, 20.0f); poff += 4;
    put_f32(parts + poff, 30.0f); poff += 4;
    poff += 6; /* orientation */

    /* xmodelsurfs: 1 mesh with bone=1 (1-based, maps to bone 0) */
    put_u16(surfs + soff, 14); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    surfs[soff++] = 0;
    put_u16(surfs + soff, 3); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    put_u16(surfs + soff, 0); soff += 2;
    put_u16(surfs + soff, 1); soff += 2; /* bone = 1 -> Bone 0 translated by (10, 20, 30) */
    surfs[soff++] = 3;                   /* strip length */
    put_u16(surfs + soff, 0); soff += 2;
    put_u16(surfs + soff, 1); soff += 2;
    put_u16(surfs + soff, 2); soff += 2;
    for (i = 0; i < 3; i++) {
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 1); soff += 4;
        put_f32(surfs + soff, 0.5f); soff += 4;
        put_f32(surfs + soff, 0.25f); soff += 4;
        put_f32(surfs + soff, (float)i); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
        put_f32(surfs + soff, 0); soff += 4;
    }

    assert(cod_xmodel_to_obj(header, hoff, surfs, soff, parts, poff, &obj, &sz) == 1);
    /* Vertex 0 (0, 0, 0) translated by (10, 20, 30) -> (10, 20, 30) */
    assert(strstr((const char*)obj, "v 10 20 30\n") != NULL);
    /* Vertex 1 (1, 0, 0) translated by (10, 20, 30) -> (11, 20, 30) */
    assert(strstr((const char*)obj, "v 11 20 30\n") != NULL);
    /* Vertex 2 (2, 0, 0) translated by (10, 20, 30) -> (12, 20, 30) */
    assert(strstr((const char*)obj, "v 12 20 30\n") != NULL);
    free(obj);
    printf("test_xmodel_v14_bones: PASS\n");
}

int main(void) {
    printf("Running cod2003 tests...\n");
    test_plugin_interface();
    test_bsp_parser();
    test_xmodel_text();
    test_xmodel_binary();
    test_xmodel_v14();
    test_xmodel_v14_bones();
    printf("All cod2003 tests passed!\n");
    return 0;
}
