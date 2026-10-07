/*
 * Tests for the modular hxfs_cod plugin. Does not touch cod2003.dll.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "archive/archive.h"
#include "bsp/read.h"
#include "bsp/names.h"
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
#include "texture/helix.h"
#include "shader/helix.h"
#include "cache.h"
#include "zip_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    expect(cod_gameplay_xmodel("models/view_mg.obj") == NULL, "view mg is not a fixed alias");
    expect(cod_gameplay_xmodel("models/player.obj") != NULL, "player body");
    expect(cod_gameplay_xmodel("models/pickup.obj") == NULL, "pickup stays stock");
    {
        unsigned char skinxm[80];
        const char* hand = "body@characterhand.dds";
        const char* body = "viewmodel@bar_body.dds";
        memset(skinxm, 0, sizeof(skinxm));
        put_u16(skinxm, 14);
        memcpy(skinxm + 2, hand, strlen(hand) + 1);
        memcpy(skinxm + 3 + strlen(hand), body, strlen(body) + 1);
        free(obj);
        obj = NULL;
        sz = 0;
        expect(cod_xmodel_to_obj(skinxm, sizeof(skinxm), surf, sizeof(surf), NULL, 0, &obj, &sz) == 1, "skin obj");
        expect(obj && strstr((const char*)obj, "usemtl skins/viewmodel@bar_body.png\n") != NULL, "body skin");
        expect(obj && strstr((const char*)obj, "characterhand") != NULL, "keep hand");
    }
    free(obj);
    report("test_xmodel_v14_surf", before);
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
    expect(text && strstr(text, "scr_foo") == NULL, "drop unknown set");
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
    unsigned char tga[18 + 3];
    unsigned char dds[128 + 8];
    unsigned char* png = NULL;
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
    memset(tga, 0, sizeof(tga));
    tga[2] = 2;
    tga[12] = 1;
    tga[14] = 1;
    tga[16] = 24;
    tga[18] = 0;
    tga[19] = 0;
    tga[20] = 255;
    expect(texture_to_png(tga, sizeof(tga), &png, &sz) == 1, "tga png");
    expect(png && sz >= 8 && png[0] == 137 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "png sig");
    free(png);
    png = NULL;
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
    expect(texture_to_png(dds, sizeof(dds), &png, &sz) == 1, "dxt1 png");
    expect(png && png[0] == 137, "dxt1 sig");
    free(png);
    png = NULL;
    {
        unsigned char dxt3[128 + 16];
        memset(dxt3, 0, sizeof(dxt3));
        memcpy(dxt3, "DDS ", 4);
        dxt3[12] = 4;
        dxt3[16] = 4;
        dxt3[80] = 0x4;
        memcpy(dxt3 + 84, "DXT3", 4);
        dxt3[128] = 0x0f;
        expect(texture_to_png(dxt3, sizeof(dxt3), &png, &sz) == 1, "dxt3 png");
        free(png);
    }
    expect(shader_source_to_helix(shader, strlen(shader), &text, &sz) == 1, "shader");
    expect(text && strstr((char*)text, "material textures/test/wall") != NULL, "mtr name");
    expect(text && strstr((char*)text, "albedo_map textures/test/bare.png") != NULL, "clamp path");
    expect(text && strstr((char*)text, "albedo_map textures/test/wall.png") != NULL, "png path");
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
        expect(text && strstr((char*)text, "albedo_map textures/test/leaf.png") != NULL, "map not editor");
        expect(text && strstr((char*)text, "editor.png") == NULL, "editor image dropped");
        expect(text && strstr((char*)text, "twoSided") != NULL, "two sided");
        expect(text && strstr((char*)text, "blend alpha") != NULL, "blend");
        expect(text && strstr((char*)text, "alphaTest 0.5") != NULL, "alpha test");
        expect(text && strstr((char*)text, "nodraw") != NULL, "caulk nodraw");
        expect(shader_is_invisible("textures/common/caulk") == 1, "caulk noted");
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
    expect(out && strstr((char*)out, "open \"map_select\"") != NULL, "uiscript startserver");
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
        expect(out && sz >= 4 && out[0] == 137 && out[1] == 'P', "dds still png");
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

int main(void) {
    test_plugin_create();
    test_archive_blob();
    test_bsp_names_and_header();
    test_xmodel_through_archive();
    test_xmodel_v14_lod();
    test_xmodel_lod_slots();
    test_xmodel_v14_surf();
    test_sound_gameplay_alias();
    test_xanim();
    test_sound_ui_gsc();
    test_texture_shader_menu();
    test_plugin_bsp_open();
    test_cvar_menu_and_cache();
    test_gsc_vm();
    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("hxfs_cod_test: PASS\n");
    return 0;
}
