/*
 * hxfs_cod4 tests — synthetic IWD only. Mods stay out of the stock mount.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "helix/fs/iasset_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#if defined(_WIN32)
#include <direct.h>
#define test_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define test_mkdir(p) mkdir((p), 0755)
#endif

extern hxAssetPlugin* helix_asset_plugin_create(void);

static int g_fails;

#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            g_fails++;                                                           \
        }                                                                        \
    } while (0)

static void wr_u16(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}
static void wr_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static int write_stored_bytes(const char* path, const char* name, const void* data,unsigned int dlen) {
    unsigned int nlen = (unsigned int)strlen(name);
    unsigned int crc = (unsigned int)crc32(0L, (const Bytef*)data, dlen);
    unsigned char buf[256];
    unsigned int pos = 0;
    FILE* f;
    if (30u + nlen + dlen + 46u + nlen + 22u > sizeof(buf)) return 0;
    wr_u32(buf + pos, 0x04034b50u); pos += 4;
    wr_u16(buf + pos, 20); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u32(buf + pos, crc); pos += 4;
    wr_u32(buf + pos, dlen); pos += 4;
    wr_u32(buf + pos, dlen); pos += 4;
    wr_u16(buf + pos, nlen); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    memcpy(buf + pos, name, nlen); pos += nlen;
    memcpy(buf + pos, data, dlen); pos += dlen;
    wr_u32(buf + pos, 0x02014b50u); pos += 4;
    wr_u16(buf + pos, 20); pos += 2;
    wr_u16(buf + pos, 20); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u32(buf + pos, crc); pos += 4;
    wr_u32(buf + pos, dlen); pos += 4;
    wr_u32(buf + pos, dlen); pos += 4;
    wr_u16(buf + pos, nlen); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u32(buf + pos, 0); pos += 4;
    wr_u32(buf + pos, 0); pos += 4;
    memcpy(buf + pos, name, nlen); pos += nlen;
    wr_u32(buf + pos, 0x06054b50u); pos += 4;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 0); pos += 2;
    wr_u16(buf + pos, 1); pos += 2;
    wr_u16(buf + pos, 1); pos += 2;
    wr_u32(buf + pos, 46u + nlen); pos += 4;
    wr_u32(buf + pos, 30u + nlen + dlen); pos += 4;
    wr_u16(buf + pos, 0); pos += 2;
    f = fopen(path, "wb");
    if (!f) return 0;
    if (fwrite(buf, 1, pos, f) != pos) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int write_stored(const char* path,const char* name,const char* data) {
    return write_stored_bytes(path,name,data,(unsigned)strlen(data));
}

static void test_stock_order_skips_mods(void) {
    const char* root = "cod4_plugin_test_tmp";
    hxAssetPlugin* plug;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;
    test_mkdir(root);
    test_mkdir("cod4_plugin_test_tmp/main");
    test_mkdir("cod4_plugin_test_tmp/Mods");
    test_mkdir("cod4_plugin_test_tmp/Mods/promod");
    CHECK(write_stored("cod4_plugin_test_tmp/main/iw_00.iwd", "images/white.iwi", "old"));
    CHECK(write_stored("cod4_plugin_test_tmp/main/iw_02.iwd", "sound/step.wav", "step"));
    CHECK(write_stored("cod4_plugin_test_tmp/main/localized_english_iw00.iwd", "images/white.iwi", "loc"));
    CHECK(write_stored("cod4_plugin_test_tmp/main/promod.iwd", "images/white.iwi", "mod"));
    CHECK(write_stored("cod4_plugin_test_tmp/Mods/promod/iw_99.iwd", "images/white.iwi", "moddir"));

    plug = helix_asset_plugin_create();
    CHECK(plug && strcmp(plug->format_name, "cod4") == 0);
    CHECK(plug->open_dir(plug, root) == 3);
    CHECK(plug->read(plug, "images/white.iwi", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "loc", 3) == 0);
    plug->free_buf(plug, buf);
    buf = NULL;
    CHECK(plug->contains(plug, "SOUND/STEP.WAV") == 1);
    plug->list(plug, &names, &count);
    CHECK(count == 2);
    plug->free_list(plug, names, count);
    CHECK(plug->open_dir(plug, "cod4_plugin_test_tmp/Mods/promod") == 0);
    CHECK(plug->contains(plug, "images/white.iwi") == 0);
    plug->destroy(plug);

    remove("cod4_plugin_test_tmp/main/iw_00.iwd");
    remove("cod4_plugin_test_tmp/main/iw_02.iwd");
    remove("cod4_plugin_test_tmp/main/localized_english_iw00.iwd");
    remove("cod4_plugin_test_tmp/main/promod.iwd");
    remove("cod4_plugin_test_tmp/Mods/promod/iw_99.iwd");
    rmdir("cod4_plugin_test_tmp/Mods/promod");
    rmdir("cod4_plugin_test_tmp/Mods");
    rmdir("cod4_plugin_test_tmp/main");
    rmdir(root);
}

extern int cod4_zone_for_name(const char* name, char* out, int cap);
extern int cod4_bsp_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name,
                             unsigned char** out_buf, unsigned int* out_size);
extern int cod4_ff_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name,
                            unsigned char** out_buf, unsigned int* out_size);

static void put_u16(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
}
static void put_u32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}
static void put_f32(unsigned char* p, float v) {
    unsigned u;
    memcpy(&u, &v, 4);
    put_u32(p, u);
}

static void test_bsp_map(void) {
    const char* ent = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"8 8 16\"\n}\n";
    unsigned ent_n = (unsigned)strlen(ent);
    unsigned char face[48];
    unsigned char verts[68 * 6];
    unsigned char indices[16];
    unsigned char file[1024];
    unsigned cursor, dir;
    unsigned char* text = NULL;
    unsigned int text_n = 0;
    hxAssetPlugin* plug;
    const char* root = "cod4_bsp_test_tmp";
    const char* bsp_path = "cod4_bsp_test_tmp/raw/maps/mp/room.d3dbsp";
    memset(face, 0, sizeof(face));
    /* Room triangle: local indices 0,1,2 plus first vertex 3. */
    put_u16(face + 12, 3);
    put_u16(face + 18, 3);
    put_u16(face + 20, 0);
    /* Sky triangle at the start of the vertex buffer. It must not be drawn. */
    put_u16(face + 24 + 18, 3);
    put_u16(face + 24 + 20, 3);
    memset(verts, 0, sizeof(verts));
    put_f32(verts + 0, 900.f);
    put_f32(verts + 8, 500.f);
    put_f32(verts + 68, 900.f);
    put_f32(verts + 72, 64.f);
    put_f32(verts + 76, 500.f);
    put_f32(verts + 136, 964.f);
    put_f32(verts + 144, 500.f);
    put_f32(verts + 68 * 3 + 0, 0.f);
    put_f32(verts + 68 * 3 + 4, 0.f);
    put_f32(verts + 68 * 3 + 8, 0.f);
    put_f32(verts + 68 * 4 + 0, 64.f);
    put_f32(verts + 68 * 4 + 4, 0.f);
    put_f32(verts + 68 * 4 + 8, 0.f);
    put_f32(verts + 68 * 5 + 0, 0.f);
    put_f32(verts + 68 * 5 + 4, 64.f);
    put_f32(verts + 68 * 5 + 8, 0.f);
    put_f32(verts + 68 * 3 + 12, 0.f);
    put_f32(verts + 68 * 3 + 16, 0.f);
    put_f32(verts + 68 * 3 + 20, 1.f);
    memset(indices, 0, sizeof(indices));
    put_u16(indices + 0, 0);
    put_u16(indices + 2, 1);
    put_u16(indices + 4, 2);
    put_u16(indices + 6, 0);
    put_u16(indices + 8, 1);
    put_u16(indices + 10, 2);
    memset(file, 0, sizeof(file));
    memcpy(file, "IBSP", 4);
    put_u32(file + 4, 22);
    put_u32(file + 8, 4);
    dir = 12;
    put_u32(file + dir, 9); put_u32(file + dir + 4, 48); dir += 8;
    put_u32(file + dir, 10); put_u32(file + dir + 4, 68 * 6); dir += 8;
    put_u32(file + dir, 11); put_u32(file + dir + 4, 12); dir += 8;
    put_u32(file + dir, 39); put_u32(file + dir + 4, ent_n); dir += 8;
    cursor = dir;
    memcpy(file + cursor, face, 48); cursor += 48;
    memcpy(file + cursor, verts, sizeof(verts)); cursor += (unsigned)sizeof(verts);
    memcpy(file + cursor, indices, 12); cursor += 12;
    cursor = (cursor + 3u) & ~3u;
    memcpy(file + cursor, ent, ent_n); cursor += ent_n;

    CHECK(cod4_bsp_to_hxmap(file, cursor, "maps/mp/room.d3dbsp", &text, &text_n) == 1);
    CHECK(text && strstr((char*)text, "f 3 4 5") && !strstr((char*)text, "f 0 1 2"));
    CHECK(strstr((char*)text, "info_player_start") && strstr((char*)text, "brush {"));
    CHECK(strstr((char*)text, "v 64 0 0"));
    CHECK(strstr((char*)text, "8 8 40"));
    free(text);
    text = NULL;
    put_u32(file + 4, 17);
    CHECK(cod4_bsp_to_hxmap(file, cursor, "room", &text, &text_n) == 0);
    put_u32(file + 4, 22);

    test_mkdir(root);
    test_mkdir("cod4_bsp_test_tmp/main");
    test_mkdir("cod4_bsp_test_tmp/raw");
    test_mkdir("cod4_bsp_test_tmp/raw/maps");
    test_mkdir("cod4_bsp_test_tmp/raw/maps/mp");
    {
        FILE* f = fopen(bsp_path, "wb");
        CHECK(f != NULL);
        if (f) {
            CHECK(fwrite(file, 1, cursor, f) == cursor);
            fclose(f);
        }
    }
    plug = helix_asset_plugin_create();
    CHECK(plug->open_dir(plug, root) == 1);
    CHECK(plug->contains(plug, "maps/mp/room.hxmap") == 1);
    CHECK(plug->contains(plug, "maps/room.hxmap") == 1);
    CHECK(plug->read(plug, "maps/mp/room.hxmap", &text, &text_n) == 1);
    CHECK(text && strstr((char*)text, "worldspawn") && strstr((char*)text, "f 3 4 5") && !strstr((char*)text, "f 0 1 2"));
    plug->free_buf(plug, text);
    plug->destroy(plug);
    remove(bsp_path);
    rmdir("cod4_bsp_test_tmp/raw/maps/mp");
    rmdir("cod4_bsp_test_tmp/raw/maps");
    rmdir("cod4_bsp_test_tmp/raw");
    rmdir("cod4_bsp_test_tmp/main");
    rmdir(root);
}

static void test_ff_map(void) {
    unsigned char zone[2048];
    unsigned char* comp = NULL;
    unsigned char* ff = NULL;
    unsigned char* text = NULL;
    unsigned int text_n = 0;
    unsigned used;
    uLongf clen;
    const unsigned char* gfx;
    const char* ent = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"8 8 16\"\n}\n{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"1 2 3\"\n}\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"4 5 6\"\n}\n";
    const char* root = "cod4_ff_test_tmp";
    hxAssetPlugin* plug;
    FILE* out;
    memset(zone, 0, sizeof(zone));
    put_u32(zone + 52, 1);
    put_u32(zone + 56, 0xffffffffu);
    put_u32(zone + 60, 16);
    put_u32(zone + 64, 0xffffffffu);
    gfx = zone + 68;
    put_u32(zone + 68 + 4, 0xffffffffu);
    put_u32(zone + 68 + 16, 3);
    put_u32(zone + 68 + 20, 0xffffffffu);
    put_u32(zone + 68 + 24, 1);
    put_u32(zone + 68 + 48, 3);
    put_u32(zone + 68 + 52, 0xffffffffu);
    put_u32(zone + 68 + 660, 0xffffffffu);
    (void)gfx;
    used = 68u + 732u;
    memcpy(zone + used, "box", 4);
    used += 4;
    put_u16(zone + used, 0);
    put_u16(zone + used + 2, 1);
    put_u16(zone + used + 4, 2);
    used += 6;
    put_f32(zone + used + 44, 64.f);
    put_f32(zone + used + 88 + 4, 64.f);
    used += 44u * 3u;
    put_u16(zone + used + 8, 3);
    put_u16(zone + used + 10, 1);
    put_f32(zone + used + 36, 64.f);
    put_f32(zone + used + 40, 64.f);
    put_f32(zone + used + 44, 1.f);
    used += 48;
    memcpy(zone + used, ent, strlen(ent));
    used += (unsigned)strlen(ent);
    CHECK(cod4_ff_to_hxmap((const unsigned char*)"IWffu100xxxx", 12, "box", &text, &text_n) == 0);
    clen = compressBound(used);
    comp = (unsigned char*)malloc(clen);
    ff = (unsigned char*)malloc(12u + clen);
    CHECK(comp && ff && compress(comp, &clen, zone, used) == Z_OK);
    if (comp && ff) {
        memcpy(ff, "IWffu100", 8);
        put_u32(ff + 8, 5);
        memcpy(ff + 12, comp, clen);
        CHECK(cod4_ff_to_hxmap(ff, 12u + (unsigned)clen, "maps/mp/mp_box.hxmap", &text, &text_n) == 1);
        CHECK(text && strstr((char*)text, "f 1 2 3") && strstr((char*)text, "v 64 0 0"));
        CHECK(text && strstr((char*)text, "8 8 40"));
        CHECK(text && strstr((char*)text, "1 2 27") && strstr((char*)text, "4 5 30"));
        CHECK(text && strstr((char*)text, "mp_deathmatch_spawn") && strstr((char*)text, "mp_teamdeathmatch_spawn"));
        {
            char* ip = strstr((char*)text, "info_player_start");
            char* dm = strstr((char*)text, "mp_dm_spawn");
            char saved;
            CHECK(ip && dm && dm > ip);
            saved = *dm;
            *dm = 0;
            CHECK(strstr(ip, "spawn_anchor") == NULL);
            *dm = saved;
        }
        free(text);
        text = NULL;
    }
    test_mkdir(root);
    test_mkdir("cod4_ff_test_tmp/main");
    test_mkdir("cod4_ff_test_tmp/zone");
    test_mkdir("cod4_ff_test_tmp/zone/english");
    out = fopen("cod4_ff_test_tmp/zone/english/mp_box.ff", "wb");
    CHECK(out != NULL);
    if (out && ff) {
        CHECK(fwrite(ff, 1, 12u + (unsigned)clen, out) == 12u + (unsigned)clen);
        fclose(out);
        out = NULL;
    } else if (out) {
        fclose(out);
    }
    plug = helix_asset_plugin_create();
    CHECK(plug->open_dir(plug, root) == 1);
    CHECK(plug->contains(plug, "maps/mp/mp_box.hxmap") == 1);
    CHECK(plug->contains(plug, "maps/mp_box.hxmap") == 1);
    CHECK(plug->read(plug, "maps/mp/mp_box.hxmap", &text, &text_n) == 1);
    CHECK(text && strstr((char*)text, "f 1 2 3"));
    if (plug && text) plug->free_buf(plug, text);
    if (plug) plug->destroy(plug);
    remove("cod4_ff_test_tmp/zone/english/mp_box.ff");
    rmdir("cod4_ff_test_tmp/zone/english");
    rmdir("cod4_ff_test_tmp/zone");
    rmdir("cod4_ff_test_tmp/main");
    rmdir(root);
    free(comp);
    free(ff);
}

static void test_iwi_alias(void) {
    const char* root = "cod4_iwi_test_tmp";
    hxAssetPlugin* plug;
    unsigned char* text = NULL;
    unsigned int text_n = 0;
    test_mkdir(root);
    test_mkdir("cod4_iwi_test_tmp/main");
    CHECK(write_stored("cod4_iwi_test_tmp/main/iw_00.iwd", "images/brick.iwi", "RGBA") == 1);
    plug = helix_asset_plugin_create();
    CHECK(plug->open_dir(plug, root) == 1);
    CHECK(plug->contains(plug, "images/brick.iwi") == 1);
    CHECK(plug->contains(plug, "images/brick_col.iwi") == 1);
    CHECK(plug->read(plug, "images/brick_col.iwi", &text, &text_n) == 1);
    CHECK(text && text_n == 4 && memcmp(text, "RGBA", 4) == 0);
    if (plug && text) plug->free_buf(plug, text);
    if (plug) plug->destroy(plug);
    {
        unsigned char iwi[32]={0};memcpy(iwi,"IWi\6",4);iwi[4]=6;iwi[5]=2;iwi[6]=iwi[8]=iwi[10]=1;
        for(unsigned i=0;i<4;i++)wr_u32(iwi+12+i*4,32);
        iwi[28]=20;iwi[29]=40;iwi[30]=60;iwi[31]=80;
        CHECK(write_stored_bytes("cod4_iwi_test_tmp/main/iw_00.iwd","images/brick.iwi",iwi,32));
        plug=helix_asset_plugin_create();CHECK(plug->open_dir(plug,root));text=NULL;text_n=0;
        CHECK(plug->read(plug,"images/brick_col.iwi",&text,&text_n));
        CHECK(text && text_n==22 && text[2]==2 && !memcmp(text+18,iwi+28,4));
        if(text)plug->free_buf(plug,text);plug->destroy(plug);
    }
    remove("cod4_iwi_test_tmp/main/iw_00.iwd");
    rmdir("cod4_iwi_test_tmp/main");
    rmdir(root);
}

static void test_zone_name(void) {
    char out[64];
    CHECK(cod4_zone_for_name("maps/mp/mp_backlot.gsc", out, (int)sizeof(out)));
    CHECK(strcmp(out, "mp_backlot.ff") == 0);
    CHECK(cod4_zone_for_name("maps/createfx/mp_backlot_fx.gsc", out, (int)sizeof(out)));
    CHECK(strcmp(out, "mp_backlot.ff") == 0);
    CHECK(cod4_zone_for_name("maps/createart/mp_backlot_art.gsc", out, (int)sizeof(out)));
    CHECK(strcmp(out, "mp_backlot.ff") == 0);
    CHECK(cod4_zone_for_name("maps/mp/gametypes/dm.gsc", out, (int)sizeof(out)) == 0);
    CHECK(cod4_zone_for_name("images/white.iwi", out, (int)sizeof(out)) == 0);
}

int main(int argc, char** argv) {
    if(argc==5 && !strcmp(argv[1],"--read")) {
        hxAssetPlugin* plug=helix_asset_plugin_create();unsigned char* data=NULL;unsigned n=0;int ok=0;FILE* out;
        if(!plug)return 1;
        if(plug->open_dir(plug,argv[2]) && plug->contains(plug,argv[3]) && plug->read(plug,argv[3],&data,&n)) {
            out=fopen(argv[4],"wb");if(out){ok=fwrite(data,1,n,out)==n;fclose(out);}
        }
        if(data)plug->free_buf(plug,data);plug->destroy(plug);
        printf("plugin resource read: %s, %u bytes\n",ok?"ok":"FAILED",n);return ok?0:1;
    }
    if(argc==4 && !strcmp(argv[1],"--original-menu")) {
        hxAssetPlugin* plug=helix_asset_plugin_create();unsigned char* data=NULL;unsigned n=0;int ok=0;FILE* out;
        if(!plug)return 1;
        if(plug->open_dir(plug,argv[2]) && plug->contains(plug,"ui_mp/cod4_original.hxmenu") &&
           plug->read(plug,"ui_mp/cod4_original.hxmenu",&data,&n)) {
            out=fopen(argv[3],"wb");if(out){ok=fwrite(data,1,n,out)==n;fclose(out);}
        }
        if(data)plug->free_buf(plug,data);plug->destroy(plug);
        printf("original menu plugin read: %s, %u bytes\n",ok?"ok":"FAILED",n);return ok?0:1;
    }
    if (argc == 3) {
        FILE* in = fopen(argv[1], "rb");
        unsigned char* data;
        unsigned char* text = NULL;
        unsigned int n = 0;
        long sz;
        FILE* out;
        if (!in) return 1;
        fseek(in, 0, SEEK_END);
        sz = ftell(in);
        fseek(in, 0, SEEK_SET);
        data = (unsigned char*)malloc((size_t)sz);
        if (!data || fread(data, 1, (size_t)sz, in) != (size_t)sz) return 1;
        fclose(in);
        if (sz >= 8 && memcmp(data, "IWffu100", 8) == 0) {
            if (!cod4_ff_to_hxmap(data, (unsigned)sz, argv[1], &text, &n)) return 1;
        } else if (!cod4_bsp_to_hxmap(data, (unsigned)sz, argv[1], &text, &n)) {
            return 1;
        }
        out = fopen(argv[2], "wb");
        if (!out || fwrite(text, 1, n, out) != n) return 1;
        fclose(out);
        printf("hxmap %u bytes\n", n);
        free(data);
        free(text);
        return 0;
    }
    test_zone_name();
    test_iwi_alias();
    test_bsp_map();
    test_ff_map();
    test_stock_order_skips_mods();
    if (g_fails) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("hxfs_cod4_test: ok\n");
    return 0;
}
