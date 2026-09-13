/*
 * hxfs_pak unit tests — synthetic PACK only (no copyrighted archives).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "pak_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern hxAssetPlugin* helix_asset_plugin_create(void);

static int g_fails;

#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            g_fails++;                                                           \
        }                                                                        \
    } while (0)

typedef struct {
    const char* name;
    const void* data;
    unsigned int size;
} TestFile;

static void wr_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static unsigned char* build_pak(const TestFile* files, int n, size_t* out_len) {
    size_t data_bytes = 0;
    size_t total;
    unsigned char* buf;
    size_t pos;
    int i;
    unsigned int dirofs;
    unsigned int dirlen;

    for (i = 0; i < n; i++) data_bytes += files[i].size;
    dirofs = (unsigned int)(12u + data_bytes);
    dirlen = (unsigned int)n * 64u;
    total = 12u + data_bytes + (size_t)dirlen;
    buf = (unsigned char*)calloc(1, total);
    if (!buf) return NULL;
    memcpy(buf, "PACK", 4);
    wr_u32(buf + 4, dirofs);
    wr_u32(buf + 8, dirlen);
    pos = 12;
    for (i = 0; i < n; i++) {
        unsigned char* e = buf + dirofs + (size_t)i * 64u;
        size_t nl = strlen(files[i].name);
        memcpy(e, files[i].name, nl > 56 ? 56 : nl);
        wr_u32(e + 56, (unsigned int)pos);
        wr_u32(e + 60, files[i].size);
        if (files[i].size && files[i].data) memcpy(buf + pos, files[i].data, files[i].size);
        pos += files[i].size;
    }
    *out_len = total;
    return buf;
}

static int write_file(const char* path, const void* p, size_t n) {
    FILE* f = fopen(path, "wb");
    size_t w;
    if (!f) return 0;
    w = fwrite(p, 1, n, f);
    fclose(f);
    return w == n;
}

static int list_has(char** names, unsigned int count, const char* want) {
    unsigned int i;
    for (i = 0; i < count; i++) {
        if (names[i] && strcmp(names[i], want) == 0) return 1;
    }
    return 0;
}

static void test_two_files(void) {
    const char bsp[] = "BSP";
    const char mdl[] = "MODEL";
    TestFile files[2];
    size_t len = 0;
    unsigned char* bytes;
    PakFile* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(files, 0, sizeof(files));
    files[0].name = "maps/e1m1.bsp";
    files[0].data = bsp;
    files[0].size = 3;
    files[1].name = "progs\\player.mdl";
    files[1].data = mdl;
    files[1].size = 5;

    bytes = build_pak(files, 2, &len);
    CHECK(bytes != NULL);
    p = pak_open_mem(bytes, len);
    free(bytes);
    CHECK(p != NULL);
    CHECK(pak_contains(p, "maps/e1m1.bsp"));
    CHECK(pak_contains(p, "MAPS/E1M1.BSP"));
    CHECK(pak_contains(p, "progs/player.mdl"));
    CHECK(!pak_contains(p, "maps/e1m2.bsp"));
    CHECK(pak_read(p, "maps/e1m1.bsp", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "BSP", 3) == 0);
    free(buf);
    pak_list(p, &names, &count);
    CHECK(count == 2);
    CHECK(list_has(names, count, "maps/e1m1.bsp"));
    CHECK(list_has(names, count, "progs/player.mdl"));
    pak_free_list(names, count);
    pak_close(p);
}

static void test_last_wins(void) {
    const char first[] = "111";
    const char second[] = "222";
    TestFile files[2];
    size_t len = 0;
    unsigned char* bytes;
    PakFile* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(files, 0, sizeof(files));
    files[0].name = "gfx/conchars";
    files[0].data = first;
    files[0].size = 3;
    files[1].name = "GFX/CONCHARS";
    files[1].data = second;
    files[1].size = 3;

    bytes = build_pak(files, 2, &len);
    CHECK(bytes != NULL);
    p = pak_open_mem(bytes, len);
    free(bytes);
    CHECK(p != NULL);
    CHECK(pak_read(p, "gfx/conchars", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "222", 3) == 0);
    free(buf);
    pak_list(p, &names, &count);
    CHECK(count == 1);
    CHECK(list_has(names, count, "gfx/conchars"));
    pak_free_list(names, count);
    pak_close(p);
}

static void test_empty_file_and_max_name(void) {
    char longname[57];
    const char payload[] = "X";
    TestFile files[2];
    size_t len = 0;
    unsigned char* bytes;
    PakFile* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;

    memset(longname, 'a', 56);
    longname[56] = 0;

    memset(files, 0, sizeof(files));
    files[0].name = "sound/misc/null.wav";
    files[0].data = NULL;
    files[0].size = 0;
    files[1].name = longname;
    files[1].data = payload;
    files[1].size = 1;

    bytes = build_pak(files, 2, &len);
    CHECK(bytes != NULL);
    p = pak_open_mem(bytes, len);
    free(bytes);
    CHECK(p != NULL);
    CHECK(pak_contains(p, "sound/misc/null.wav"));
    CHECK(pak_read(p, "sound/misc/null.wav", &buf, &sz));
    CHECK(sz == 0 && buf != NULL);
    free(buf);
    CHECK(pak_contains(p, longname));
    CHECK(pak_read(p, longname, &buf, &sz));
    CHECK(sz == 1 && buf && buf[0] == 'X');
    free(buf);
    pak_close(p);
}

static void test_bad_magic_and_missing(void) {
    const unsigned char junk[] = "XXXX\x00\x00\x00\x00\x00\x00\x00\x00";
    PakFile* p = pak_open_mem(junk, sizeof(junk) - 1);
    CHECK(p == NULL);
    CHECK(pak_open_file("definitely_missing_hxfs_pak.pak") == NULL);
}

static void test_plugin_open_replace(void) {
    const char hello[] = "aaa";
    const char world[] = "bbb";
    TestFile a[1], b[1];
    size_t la = 0, lb = 0;
    unsigned char *ba, *bb;
    hxAssetPlugin* plug;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;
    const char* path_a = "hxfs_pak_test_a.pak";
    const char* path_b = "hxfs_pak_test_b.pak";

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    a[0].name = "foo.txt";
    a[0].data = hello;
    a[0].size = 3;
    b[0].name = "bar.txt";
    b[0].data = world;
    b[0].size = 3;
    ba = build_pak(a, 1, &la);
    bb = build_pak(b, 1, &lb);
    CHECK(ba && bb);
    CHECK(write_file(path_a, ba, la));
    CHECK(write_file(path_b, bb, lb));
    free(ba);
    free(bb);

    plug = helix_asset_plugin_create();
    CHECK(plug != NULL);
    CHECK(plug->api_version == HELIX_ASSET_PLUGIN_API_VERSION);
    CHECK(plug->format_name && strcmp(plug->format_name, "pak") == 0);
    CHECK(plug->open(plug, "definitely_missing_hxfs_pak.pak") == 0);
    CHECK(plug->open(plug, path_a) == 1);
    CHECK(plug->contains(plug, "foo.txt") == 1);
    CHECK(plug->read(plug, "foo.txt", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "aaa", 3) == 0);
    plug->free_buf(plug, buf);
    plug->list(plug, &names, &count);
    CHECK(count == 1 && list_has(names, count, "foo.txt"));
    plug->free_list(plug, names, count);

    CHECK(plug->open(plug, path_b) == 1);
    CHECK(plug->contains(plug, "foo.txt") == 0);
    CHECK(plug->contains(plug, "bar.txt") == 1);
    buf = NULL;
    sz = 0;
    CHECK(plug->read(plug, "bar.txt", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "bbb", 3) == 0);
    plug->free_buf(plug, buf);

    plug->close(plug);
    plug->destroy(plug);
    remove(path_a);
    remove(path_b);
}

int main(void) {
    test_two_files();
    test_last_wins();
    test_empty_file_and_max_name();
    test_bad_magic_and_missing();
    test_plugin_open_replace();
    if (g_fails) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("hxfs_pak_test: ok\n");
    return 0;
}
