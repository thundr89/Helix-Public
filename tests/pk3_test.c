/*
 * hxfs_pk3 unit tests — synthetic ZIP only (no copyrighted archives).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "pk3_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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
    int deflate;
} TestFile;

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

static int raw_deflate(const unsigned char* in, unsigned int inlen, unsigned char** out, unsigned int* outlen) {
    z_stream s;
    unsigned int cap;
    unsigned char* buf;
    int r;
    memset(&s, 0, sizeof(s));
    cap = inlen + 64u;
    buf = (unsigned char*)malloc(cap);
    if (!buf) return 0;
    if (deflateInit2(&s, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(buf);
        return 0;
    }
    s.next_in = (Bytef*)in;
    s.avail_in = inlen;
    s.next_out = buf;
    s.avail_out = cap;
    r = deflate(&s, Z_FINISH);
    if (r != Z_STREAM_END) {
        deflateEnd(&s);
        free(buf);
        return 0;
    }
    *outlen = (unsigned int)s.total_out;
    *out = buf;
    deflateEnd(&s);
    return 1;
}

typedef struct {
    unsigned int method;
    unsigned int crc;
    unsigned int csize;
    unsigned int usize;
    unsigned int local_ofs;
    unsigned int namelen;
    unsigned char* payload;
} BuiltFile;

static unsigned char* build_zip(const TestFile* files, int n, size_t* out_len) {
    BuiltFile* built;
    size_t local_bytes = 0;
    size_t cd_bytes = 0;
    size_t total;
    unsigned char* buf;
    size_t pos;
    unsigned int cd_ofs;
    int i;

    built = (BuiltFile*)calloc((size_t)n, sizeof(BuiltFile));
    if (!built) return NULL;

    for (i = 0; i < n; i++) {
        unsigned int namelen = (unsigned int)strlen(files[i].name);
        built[i].usize = files[i].size;
        built[i].crc = (unsigned int)crc32(0L, files[i].size ? (const Bytef*)files[i].data : Z_NULL, files[i].size);
        built[i].namelen = namelen;
        if (files[i].deflate && files[i].size) {
            if (!raw_deflate((const unsigned char*)files[i].data, files[i].size, &built[i].payload, &built[i].csize)) {
                int j;
                for (j = 0; j < i; j++) free(built[j].payload);
                free(built);
                return NULL;
            }
            built[i].method = 8;
        } else {
            built[i].csize = files[i].size;
            built[i].payload = NULL;
            built[i].method = 0;
        }
        local_bytes += 30u + namelen + built[i].csize;
        cd_bytes += 46u + namelen;
    }

    cd_ofs = (unsigned int)local_bytes;
    total = local_bytes + cd_bytes + 22u;
    buf = (unsigned char*)calloc(1, total);
    if (!buf) {
        for (i = 0; i < n; i++) free(built[i].payload);
        free(built);
        return NULL;
    }

    pos = 0;
    for (i = 0; i < n; i++) {
        const unsigned char* payload = built[i].payload ? built[i].payload : (const unsigned char*)files[i].data;
        built[i].local_ofs = (unsigned int)pos;
        wr_u32(buf + pos, 0x04034b50u);
        wr_u16(buf + pos + 4, 20);
        wr_u16(buf + pos + 6, 0);
        wr_u16(buf + pos + 8, built[i].method);
        wr_u16(buf + pos + 10, 0);
        wr_u16(buf + pos + 12, 0);
        wr_u32(buf + pos + 14, built[i].crc);
        wr_u32(buf + pos + 18, built[i].csize);
        wr_u32(buf + pos + 22, built[i].usize);
        wr_u16(buf + pos + 26, built[i].namelen);
        wr_u16(buf + pos + 28, 0);
        memcpy(buf + pos + 30, files[i].name, built[i].namelen);
        if (built[i].csize && payload) memcpy(buf + pos + 30 + built[i].namelen, payload, built[i].csize);
        pos += 30u + built[i].namelen + built[i].csize;
    }

    for (i = 0; i < n; i++) {
        wr_u32(buf + pos, 0x02014b50u);
        wr_u16(buf + pos + 4, 20);
        wr_u16(buf + pos + 6, 20);
        wr_u16(buf + pos + 8, 0);
        wr_u16(buf + pos + 10, built[i].method);
        wr_u16(buf + pos + 12, 0);
        wr_u16(buf + pos + 14, 0);
        wr_u32(buf + pos + 16, built[i].crc);
        wr_u32(buf + pos + 20, built[i].csize);
        wr_u32(buf + pos + 24, built[i].usize);
        wr_u16(buf + pos + 28, built[i].namelen);
        wr_u16(buf + pos + 30, 0);
        wr_u16(buf + pos + 32, 0);
        wr_u16(buf + pos + 34, 0);
        wr_u16(buf + pos + 36, 0);
        wr_u32(buf + pos + 38, 0);
        wr_u32(buf + pos + 42, built[i].local_ofs);
        memcpy(buf + pos + 46, files[i].name, built[i].namelen);
        pos += 46u + built[i].namelen;
        free(built[i].payload);
    }
    free(built);

    wr_u32(buf + pos, 0x06054b50u);
    wr_u16(buf + pos + 4, 0);
    wr_u16(buf + pos + 6, 0);
    wr_u16(buf + pos + 8, (unsigned int)n);
    wr_u16(buf + pos + 10, (unsigned int)n);
    wr_u32(buf + pos + 12, (unsigned int)cd_bytes);
    wr_u32(buf + pos + 16, cd_ofs);
    wr_u16(buf + pos + 20, 0);
    pos += 22;
    *out_len = pos;
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

static void test_store_two_files(void) {
    const char bsp[] = "BSP";
    const char tga[] = "TGAIMG";
    TestFile files[2];
    size_t len = 0;
    unsigned char* bytes;
    Pk3File* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(files, 0, sizeof(files));
    files[0].name = "maps/q3dm1.bsp";
    files[0].data = bsp;
    files[0].size = 3;
    files[1].name = "textures\\base.tga";
    files[1].data = tga;
    files[1].size = 6;

    bytes = build_zip(files, 2, &len);
    CHECK(bytes != NULL);
    p = pk3_open_mem(bytes, len);
    free(bytes);
    CHECK(p != NULL);
    CHECK(pk3_contains(p, "maps/q3dm1.bsp"));
    CHECK(pk3_contains(p, "MAPS/Q3DM1.BSP"));
    CHECK(pk3_contains(p, "textures/base.tga"));
    CHECK(!pk3_contains(p, "maps/q3dm2.bsp"));
    CHECK(pk3_read(p, "maps/q3dm1.bsp", &buf, &sz));
    CHECK(sz == 3 && buf && memcmp(buf, "BSP", 3) == 0);
    free(buf);
    pk3_list(p, &names, &count);
    CHECK(count == 2);
    CHECK(list_has(names, count, "maps/q3dm1.bsp"));
    CHECK(list_has(names, count, "textures/base.tga"));
    pk3_free_list(names, count);
    pk3_close(p);
}

static void test_deflate_and_last_wins(void) {
    const char first[] = "111";
    const char hello[] = "hello pk3";
    TestFile files[3];
    size_t len = 0;
    unsigned char* bytes;
    Pk3File* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    memset(files, 0, sizeof(files));
    files[0].name = "scripts/base.shader";
    files[0].data = first;
    files[0].size = 3;
    files[0].deflate = 0;
    files[1].name = "SCRIPTS/base.shader";
    files[1].data = hello;
    files[1].size = (unsigned int)strlen(hello);
    files[1].deflate = 1;
    files[2].name = "textures/";
    files[2].data = NULL;
    files[2].size = 0;

    bytes = build_zip(files, 3, &len);
    CHECK(bytes != NULL);
    p = pk3_open_mem(bytes, len);
    free(bytes);
    CHECK(p != NULL);
    CHECK(pk3_read(p, "scripts/base.shader", &buf, &sz));
    CHECK(sz == (unsigned int)strlen(hello) && buf && memcmp(buf, hello, sz) == 0);
    free(buf);
    CHECK(!pk3_contains(p, "textures/"));
    CHECK(!pk3_contains(p, "textures"));
    pk3_list(p, &names, &count);
    CHECK(count == 1);
    CHECK(list_has(names, count, "scripts/base.shader"));
    pk3_free_list(names, count);
    pk3_close(p);
}

static void test_bad_magic_and_missing(void) {
    const unsigned char junk[] = "PK\x03\x04XXXX";
    Pk3File* p = pk3_open_mem(junk, sizeof(junk) - 1);
    CHECK(p == NULL);
    CHECK(pk3_open_file("definitely_missing_hxfs_pk3.pk3") == NULL);
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
    const char* path_a = "hxfs_pk3_test_a.pk3";
    const char* path_b = "hxfs_pk3_test_b.pk3";

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    a[0].name = "foo.txt";
    a[0].data = hello;
    a[0].size = 3;
    a[0].deflate = 1;
    b[0].name = "bar.txt";
    b[0].data = world;
    b[0].size = 3;

    ba = build_zip(a, 1, &la);
    bb = build_zip(b, 1, &lb);
    CHECK(ba && bb);
    CHECK(write_file(path_a, ba, la));
    CHECK(write_file(path_b, bb, lb));
    free(ba);
    free(bb);

    plug = helix_asset_plugin_create();
    CHECK(plug != NULL);
    CHECK(plug->api_version == HELIX_ASSET_PLUGIN_API_VERSION);
    CHECK(plug->format_name && strcmp(plug->format_name, "pk3") == 0);
    CHECK(plug->open(plug, "definitely_missing_hxfs_pk3.pk3") == 0);
    CHECK(plug->open(plug, path_a) == 1);
    CHECK(plug->contains(plug, "foo.txt") == 1);
    CHECK(plug->read(plug, "FOO.TXT", &buf, &sz) == 1);
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
    test_store_two_files();
    test_deflate_and_last_wins();
    test_bad_magic_and_missing();
    test_plugin_open_replace();
    if (g_fails) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("hxfs_pk3_test: ok\n");
    return 0;
}
