/*
 * hxfs_wad unit tests — synthetic IWAD / WAD2 only (no copyrighted archives).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "wad_file.h"

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
    unsigned char type;
    unsigned char compression;
} TestLump;

static void wr_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static unsigned char* build_wad1(const char* magic, const TestLump* lumps, int n, size_t* out_len) {
    size_t data_bytes = 0;
    size_t total;
    unsigned char* buf;
    size_t pos;
    int i;
    unsigned int dirofs;

    for (i = 0; i < n; i++) data_bytes += lumps[i].size;
    dirofs = (unsigned int)(12u + data_bytes);
    total = 12u + data_bytes + (size_t)n * 16u;
    buf = (unsigned char*)calloc(1, total);
    if (!buf) return NULL;
    memcpy(buf, magic, 4);
    wr_u32(buf + 4, (unsigned int)n);
    wr_u32(buf + 8, dirofs);
    pos = 12;
    for (i = 0; i < n; i++) {
        unsigned char* e = buf + dirofs + (size_t)i * 16u;
        wr_u32(e + 0, (unsigned int)pos);
        wr_u32(e + 4, lumps[i].size);
        memcpy(e + 8, lumps[i].name, strlen(lumps[i].name) > 8 ? 8 : strlen(lumps[i].name));
        if (lumps[i].size && lumps[i].data) memcpy(buf + pos, lumps[i].data, lumps[i].size);
        pos += lumps[i].size;
    }
    *out_len = total;
    return buf;
}

static unsigned char* build_wad2(const char* magic, const TestLump* lumps, int n, size_t* out_len) {
    size_t data_bytes = 0;
    size_t total;
    unsigned char* buf;
    size_t pos;
    int i;
    unsigned int dirofs;

    for (i = 0; i < n; i++) data_bytes += lumps[i].size;
    dirofs = (unsigned int)(12u + data_bytes);
    total = 12u + data_bytes + (size_t)n * 32u;
    buf = (unsigned char*)calloc(1, total);
    if (!buf) return NULL;
    memcpy(buf, magic, 4);
    wr_u32(buf + 4, (unsigned int)n);
    wr_u32(buf + 8, dirofs);
    pos = 12;
    for (i = 0; i < n; i++) {
        unsigned char* e = buf + dirofs + (size_t)i * 32u;
        size_t nl = strlen(lumps[i].name);
        wr_u32(e + 0, (unsigned int)pos);
        wr_u32(e + 4, lumps[i].size);
        wr_u32(e + 8, lumps[i].size);
        e[12] = lumps[i].type;
        e[13] = lumps[i].compression;
        memcpy(e + 16, lumps[i].name, nl > 16 ? 16 : nl);
        if (lumps[i].size && lumps[i].data) memcpy(buf + pos, lumps[i].data, lumps[i].size);
        pos += lumps[i].size;
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

static void test_iwad_two_lumps(void) {
    const char hello[] = "hi";
    const char world[] = "there";
    TestLump lumps[2];
    size_t len = 0;
    unsigned char* bytes;
    WadFile* w;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(lumps, 0, sizeof(lumps));
    lumps[0].name = "HELLO";
    lumps[0].data = hello;
    lumps[0].size = 2;
    lumps[1].name = "WORLD";
    lumps[1].data = world;
    lumps[1].size = 5;

    bytes = build_wad1("IWAD", lumps, 2, &len);
    CHECK(bytes != NULL);
    w = wad_open_mem(bytes, len);
    free(bytes);
    CHECK(w != NULL);
    CHECK(wad_contains(w, "hello"));
    CHECK(wad_contains(w, "HELLO"));
    CHECK(wad_contains(w, "world"));
    CHECK(!wad_contains(w, "nope"));
    CHECK(wad_read(w, "hello", &buf, &sz));
    CHECK(sz == 2);
    CHECK(buf && memcmp(buf, "hi", 2) == 0);
    free(buf);
    wad_list(w, &names, &count);
    CHECK(count == 2);
    CHECK(list_has(names, count, "hello"));
    CHECK(list_has(names, count, "world"));
    wad_free_list(names, count);
    wad_close(w);
}

static void test_iwad_two_maps(void) {
    const char a[] = "AAA";
    const char b[] = "BBB";
    TestLump lumps[4];
    size_t len = 0;
    unsigned char* bytes;
    WadFile* w;
    unsigned char* buf = NULL;
    unsigned int sz = 0;

    memset(lumps, 0, sizeof(lumps));
    lumps[0].name = "E1M1";
    lumps[0].data = NULL;
    lumps[0].size = 0;
    lumps[1].name = "THINGS";
    lumps[1].data = a;
    lumps[1].size = 3;
    lumps[2].name = "E1M2";
    lumps[2].data = NULL;
    lumps[2].size = 0;
    lumps[3].name = "THINGS";
    lumps[3].data = b;
    lumps[3].size = 3;

    bytes = build_wad1("IWAD", lumps, 4, &len);
    CHECK(bytes != NULL);
    w = wad_open_mem(bytes, len);
    free(bytes);
    CHECK(w != NULL);
    CHECK(wad_contains(w, "maps/e1m1/things"));
    CHECK(wad_contains(w, "MAPS/E1M1/THINGS"));
    CHECK(wad_contains(w, "maps/e1m2/things"));
    CHECK(!wad_contains(w, "things"));
    CHECK(wad_read(w, "maps/e1m1/things", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "AAA", 3) == 0);
    free(buf);
    buf = NULL;
    CHECK(wad_read(w, "maps/e1m2/things", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "BBB", 3) == 0);
    free(buf);
    wad_close(w);
}

static void test_wad2_miptex(void) {
    const char payload[] = "MIP";
    TestLump lumps[1];
    size_t len = 0;
    unsigned char* bytes;
    WadFile* w;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(lumps, 0, sizeof(lumps));
    lumps[0].name = "1234567890123456"; /* 16 chars, no NUL in directory */
    lumps[0].data = payload;
    lumps[0].size = 3;
    lumps[0].type = 67; /* miptex */

    bytes = build_wad2("WAD2", lumps, 1, &len);
    CHECK(bytes != NULL);
    w = wad_open_mem(bytes, len);
    free(bytes);
    CHECK(w != NULL);
    CHECK(wad_contains(w, "textures/1234567890123456"));
    CHECK(wad_read(w, "textures/1234567890123456", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "MIP", 3) == 0);
    free(buf);
    wad_list(w, &names, &count);
    CHECK(count == 1);
    CHECK(list_has(names, count, "textures/1234567890123456"));
    wad_free_list(names, count);
    wad_close(w);
}

static void test_last_wins_and_flats(void) {
    const char first[] = "111";
    const char second[] = "222";
    const char floor[] = "FLAT";
    TestLump lumps[5];
    size_t len = 0;
    unsigned char* bytes;
    WadFile* w;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(lumps, 0, sizeof(lumps));
    lumps[0].name = "PLAYPAL";
    lumps[0].data = first;
    lumps[0].size = 3;
    lumps[1].name = "PLAYPAL";
    lumps[1].data = second;
    lumps[1].size = 3;
    lumps[2].name = "F_START";
    lumps[2].size = 0;
    lumps[3].name = "FLOOR0_1";
    lumps[3].data = floor;
    lumps[3].size = 4;
    lumps[4].name = "F_END";
    lumps[4].size = 0;

    bytes = build_wad1("PWAD", lumps, 5, &len);
    CHECK(bytes != NULL);
    w = wad_open_mem(bytes, len);
    free(bytes);
    CHECK(w != NULL);
    CHECK(wad_read(w, "playpal", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "222", 3) == 0);
    free(buf);
    CHECK(wad_contains(w, "flats/floor0_1"));
    CHECK(!wad_contains(w, "f_start"));
    wad_list(w, &names, &count);
    CHECK(count == 2);
    CHECK(list_has(names, count, "playpal"));
    CHECK(list_has(names, count, "flats/floor0_1"));
    wad_free_list(names, count);
    wad_close(w);
}

static void test_bad_magic_and_missing(void) {
    const unsigned char junk[] = "XXXX\x00\x00\x00\x00\x00\x00\x00\x00";
    WadFile* w = wad_open_mem(junk, sizeof(junk) - 1);
    CHECK(w == NULL);
    CHECK(wad_open_file("definitely_missing_hxfs_wad.wad") == NULL);
}

static void test_plugin_open_replace(void) {
    const char hello[] = "aaa";
    const char world[] = "bbb";
    TestLump a[1], b[1];
    size_t la = 0, lb = 0;
    unsigned char *ba, *bb;
    hxAssetPlugin* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;
    const char* path_a = "hxfs_wad_test_a.wad";
    const char* path_b = "hxfs_wad_test_b.wad";

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    a[0].name = "FOO";
    a[0].data = hello;
    a[0].size = 3;
    b[0].name = "BAR";
    b[0].data = world;
    b[0].size = 3;
    ba = build_wad1("IWAD", a, 1, &la);
    bb = build_wad1("IWAD", b, 1, &lb);
    CHECK(ba && bb);
    CHECK(write_file(path_a, ba, la));
    CHECK(write_file(path_b, bb, lb));
    free(ba);
    free(bb);

    p = helix_asset_plugin_create();
    CHECK(p != NULL);
    CHECK(p->api_version == HELIX_ASSET_PLUGIN_API_VERSION);
    CHECK(p->format_name && strcmp(p->format_name, "wad") == 0);
    CHECK(p->open(p, "definitely_missing_hxfs_wad.wad") == 0);
    CHECK(p->open(p, path_a) == 1);
    CHECK(p->contains(p, "foo") == 1);
    CHECK(p->read(p, "foo", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "aaa", 3) == 0);
    p->free_buf(p, buf);
    p->list(p, &names, &count);
    CHECK(count == 1 && list_has(names, count, "foo"));
    p->free_list(p, names, count);

    CHECK(p->open(p, path_b) == 1);
    CHECK(p->contains(p, "foo") == 0);
    CHECK(p->contains(p, "bar") == 1);
    buf = NULL;
    sz = 0;
    CHECK(p->read(p, "bar", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "bbb", 3) == 0);
    p->free_buf(p, buf);

    p->close(p);
    p->destroy(p);
    remove(path_a);
    remove(path_b);
}

static void test_wad2_compressed_skipped(void) {
    const char payload[] = "X";
    TestLump lumps[1];
    size_t len = 0;
    unsigned char* bytes;
    WadFile* w;

    memset(lumps, 0, sizeof(lumps));
    lumps[0].name = "PACKED";
    lumps[0].data = payload;
    lumps[0].size = 1;
    lumps[0].type = 67;
    lumps[0].compression = 1;

    bytes = build_wad2("WAD2", lumps, 1, &len);
    CHECK(bytes != NULL);
    w = wad_open_mem(bytes, len);
    free(bytes);
    CHECK(w != NULL);
    CHECK(!wad_contains(w, "textures/packed"));
    CHECK(!wad_contains(w, "packed"));
    wad_close(w);
}

int main(void) {
    test_iwad_two_lumps();
    test_iwad_two_maps();
    test_wad2_miptex();
    test_last_wins_and_flats();
    test_bad_magic_and_missing();
    test_plugin_open_replace();
    test_wad2_compressed_skipped();
    if (g_fails) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("hxfs_wad_test: ok\n");
    return 0;
}
