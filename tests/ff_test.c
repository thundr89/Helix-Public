/*
 * hxfs_ff unit tests — synthetic IWff only (no copyrighted archives).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "ff_file.h"

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

static void wr_u16be(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)((v >> 8) & 0xffu);
    p[1] = (unsigned char)(v & 0xffu);
}

static void wr_u32le(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static void wr_u32be(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)((v >> 24) & 0xffu);
    p[1] = (unsigned char)((v >> 16) & 0xffu);
    p[2] = (unsigned char)((v >> 8) & 0xffu);
    p[3] = (unsigned char)(v & 0xffu);
}

static void wr_u32(unsigned char* p, unsigned int v, int be) {
    if (be) wr_u32be(p, v);
    else wr_u32le(p, v);
}

static unsigned char* zlib_bytes(const unsigned char* in, size_t inlen, uLong* out_clen) {
    uLong clen = compressBound((uLong)inlen);
    unsigned char* cbuf = (unsigned char*)malloc(clen);
    if (!cbuf) return NULL;
    if (compress(cbuf, &clen, in, (uLong)inlen) != Z_OK) {
        free(cbuf);
        return NULL;
    }
    *out_clen = clen;
    return cbuf;
}

/* IW3 12-byte rawfile: { -1, len, -1, name, data } */
static unsigned char* build_zone_iw3(const char* name, const void* payload, unsigned int plen, int be, size_t* out_len) {
    size_t nlen = strlen(name);
    size_t total = 44u + 16u + 8u + 4u + 12u + nlen + 1u + plen;
    unsigned char* z = (unsigned char*)calloc(1, total);
    size_t pos;
    if (!z) return NULL;
    wr_u32(z + 0, (unsigned int)(total - 44u), be);
    pos = 44;
    wr_u32(z + pos + 8, 1, be);            /* num_assets */
    wr_u32(z + pos + 12, 0xFFFFFFFFu, be); /* unused */
    pos += 16;
    wr_u32(z + pos, 0x1Fu, be);            /* RAWFILE */
    wr_u32(z + pos + 4, 0xFFFFFFFFu, be);
    pos += 8;
    wr_u32(z + pos, 0xFFFFFFFFu, be);      /* terminator */
    pos += 4;
    wr_u32(z + pos, 0xFFFFFFFFu, be);      /* RawFile.name */
    wr_u32(z + pos + 4, plen, be);
    wr_u32(z + pos + 8, 0xFFFFFFFFu, be);  /* RawFile.buffer */
    pos += 12;
    memcpy(z + pos, name, nlen + 1);
    pos += nlen + 1;
    if (plen) memcpy(z + pos, payload, plen);
    pos += plen;
    *out_len = pos;
    return z;
}

/* IW4 16-byte rawfile: { -1, compressedLen, len, -1, name, data } */
static unsigned char* build_zone_iw4(const char* name, const void* payload, unsigned int plen, int inner_zlib, size_t* out_len) {
    size_t nlen = strlen(name);
    unsigned char* data = (unsigned char*)payload;
    unsigned int dlen = plen;
    unsigned char* zblob = NULL;
    uLong zclen = 0;
    unsigned char* z;
    size_t pos;
    size_t total;

    if (inner_zlib) {
        zblob = zlib_bytes((const unsigned char*)payload, plen, &zclen);
        if (!zblob) return NULL;
        data = zblob;
        dlen = (unsigned int)zclen;
    }
    total = 16u + nlen + 1u + dlen;
    z = (unsigned char*)calloc(1, total);
    if (!z) {
        free(zblob);
        return NULL;
    }
    wr_u32le(z + 0, 0xFFFFFFFFu);
    wr_u32le(z + 4, inner_zlib ? dlen : 0);
    wr_u32le(z + 8, plen);
    wr_u32le(z + 12, 0xFFFFFFFFu);
    pos = 16;
    memcpy(z + pos, name, nlen + 1);
    pos += nlen + 1;
    if (dlen) memcpy(z + pos, data, dlen);
    pos += dlen;
    free(zblob);
    *out_len = pos;
    return z;
}

static unsigned char* wrap_zlib_ff(const unsigned char* zone, size_t zlen, unsigned int version, int be, unsigned int extra, const char magic[8], size_t* out_len) {
    uLong clen = 0;
    unsigned char* cbuf = zlib_bytes(zone, zlen, &clen);
    unsigned char* ff;
    size_t hdr = 12u + extra;
    if (!cbuf) return NULL;
    ff = (unsigned char*)malloc(hdr + clen);
    if (!ff) {
        free(cbuf);
        return NULL;
    }
    memcpy(ff, magic, 8);
    wr_u32(ff + 8, version, be);
    if (extra) memset(ff + 12, 0, extra);
    memcpy(ff + hdr, cbuf, clen);
    free(cbuf);
    *out_len = hdr + clen;
    return ff;
}

static int raw_deflate(const unsigned char* in, unsigned int inlen, unsigned char** out, unsigned int* outlen) {
    z_stream s;
    unsigned int cap = inlen + 64u;
    unsigned char* buf;
    int r;
    memset(&s, 0, sizeof(s));
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

static unsigned char* wrap_blocks_ff(const unsigned char* zone, size_t zlen, size_t* out_len) {
    unsigned char* packed = NULL;
    unsigned char* ff;
    size_t used = 0;
    size_t cap = zlen + 64u;
    size_t off = 0;
    packed = (unsigned char*)malloc(cap);
    if (!packed) return NULL;
    while (off < zlen) {
        unsigned int chunk = (unsigned int)((zlen - off > 65536u) ? 65536u : (zlen - off));
        unsigned char* cdata = NULL;
        unsigned int clen = 0;
        if (!raw_deflate(zone + off, chunk, &cdata, &clen)) {
            free(packed);
            return NULL;
        }
        if (used + 2u + clen + 2u > cap) {
            size_t ncap = (used + 2u + clen + 2u) * 2u;
            unsigned char* n = (unsigned char*)realloc(packed, ncap);
            if (!n) {
                free(cdata);
                free(packed);
                return NULL;
            }
            packed = n;
            cap = ncap;
        }
        wr_u16be(packed + used, clen);
        memcpy(packed + used + 2, cdata, clen);
        used += 2u + clen;
        free(cdata);
        off += chunk;
    }
    wr_u16be(packed + used, 1); /* end marker 0x0001 */
    used += 2;
    ff = (unsigned char*)malloc(12u + used);
    if (!ff) {
        free(packed);
        return NULL;
    }
    memcpy(ff, "IWffu100", 8);
    wr_u32be(ff + 8, 1); /* iw3-xenon */
    memcpy(ff + 12, packed, used);
    free(packed);
    *out_len = 12u + used;
    return ff;
}

static unsigned char* wrap_iw4_authed(const unsigned char* zone, size_t zlen, size_t* out_len) {
    uLong clen = 0;
    unsigned char* cbuf = zlib_bytes(zone, zlen, &clen);
    unsigned char* ff;
    size_t extra = 9;
    size_t auth = 0x2000u;
    size_t hash = 0x2000u;
    size_t hdr = 12u + extra;
    size_t total;
    if (!cbuf) return NULL;
    total = hdr + auth + hash + clen;
    ff = (unsigned char*)calloc(1, total);
    if (!ff) {
        free(cbuf);
        return NULL;
    }
    memcpy(ff, "IWff0100", 8);
    wr_u32le(ff + 8, 0x114u);
    memcpy(ff + hdr, "IWffs100", 8);
    memcpy(ff + hdr + auth + hash, cbuf, clen);
    free(cbuf);
    *out_len = total;
    return ff;
}

static int write_file(const char* path, const void* p, size_t n) {
    FILE* f = fopen(path, "wb");
    size_t w;
    if (!f) return 0;
    w = fwrite(p, 1, n, f);
    fclose(f);
    return w == n;
}

static void expect_rawfile(FfFile* p, const char* name, const char* payload) {
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    CHECK(ff_contains(p, name));
    CHECK(ff_read(p, name, &buf, &sz));
    CHECK(sz == (unsigned int)strlen(payload) && buf && memcmp(buf, payload, sz) == 0);
    free(buf);
}

static void test_iw3_pc(void) {
    const char payload[] = "callback();";
    const char* rname = "mp/gametypes/_callbacksetup.gsc";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    char** names = NULL;
    unsigned int count = 0;

    zone = build_zone_iw3(rname, payload, (unsigned int)strlen(payload), 0, &zlen);
    CHECK(zone != NULL);
    ff = wrap_zlib_ff(zone, zlen, 5, 0, 0, "IWffu100", &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "iw3-pc") == 0);
    CHECK(ff_version(p) == 5);
    CHECK(ff_big_endian(p) == 0);
    CHECK(ff_contains(p, "zone.bin"));
    CHECK(ff_contains(p, "ff.profile"));
    expect_rawfile(p, rname, payload);
    CHECK(ff_contains(p, "MP/GAMETYPES/_CALLBACKSETUP.GSC"));
    CHECK(ff_read(p, "ff.profile", &buf, &sz));
    CHECK(sz > 0 && buf && memcmp(buf, "iw3-pc ", 7) == 0);
    free(buf);
    ff_list(p, &names, &count);
    CHECK(count >= 3);
    ff_free_list(names, count);
    ff_close(p);
}

static void test_iw3_xenon(void) {
    const char payload[] = "bind TAB +scores";
    const char* rname = "players/default.cfg";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;

    zone = build_zone_iw3(rname, payload, (unsigned int)strlen(payload), 1, &zlen);
    CHECK(zone != NULL);
    ff = wrap_blocks_ff(zone, zlen, &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "iw3-xenon") == 0);
    CHECK(ff_version(p) == 1);
    CHECK(ff_big_endian(p) == 1);
    expect_rawfile(p, rname, payload);
    ff_close(p);
}

static void test_waw_pc(void) {
    const char payload[] = "maps\\mp\\_load.gsc";
    const char* rname = "maps/mp/_load.gsc";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;

    zone = build_zone_iw3(rname, payload, (unsigned int)strlen(payload), 0, &zlen);
    CHECK(zone != NULL);
    ff = wrap_zlib_ff(zone, zlen, 0x183u, 0, 0, "IWffu100", &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "waw-pc") == 0);
    CHECK(ff_version(p) == 0x183u);
    expect_rawfile(p, rname, payload);
    ff_close(p);
}

static void test_iw4_pc(void) {
    const char payload[] = "waittillframeend;";
    const char* rname = "maps/mp/_utility.gsc";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;

    zone = build_zone_iw4(rname, payload, (unsigned int)strlen(payload), 0, &zlen);
    CHECK(zone != NULL);
    ff = wrap_zlib_ff(zone, zlen, 0x114u, 0, 9, "IWffu100", &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "iw4-pc") == 0);
    CHECK(ff_version(p) == 0x114u);
    CHECK(ff_big_endian(p) == 0);
    expect_rawfile(p, rname, payload);
    ff_close(p);
}

static void test_iw4_inner_zlib(void) {
    const char payload[] = "init();";
    const char* rname = "scripts/mp/init.gsc";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;

    zone = build_zone_iw4(rname, payload, (unsigned int)strlen(payload), 1, &zlen);
    CHECK(zone != NULL);
    ff = wrap_zlib_ff(zone, zlen, 0x114u, 0, 9, "IWffu100", &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "iw4-pc") == 0);
    expect_rawfile(p, rname, payload);
    ff_close(p);
}

static void test_iw4_authed(void) {
    const char payload[] = "level.callbackStartGameType = ::Callback_StartGameType;";
    const char* rname = "maps/mp/gametypes/_callbacksetup.gsc";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    FfFile* p;

    zone = build_zone_iw4(rname, payload, (unsigned int)strlen(payload), 0, &zlen);
    CHECK(zone != NULL);
    ff = wrap_iw4_authed(zone, zlen, &flen);
    free(zone);
    CHECK(ff != NULL);
    p = ff_open_mem(ff, flen);
    free(ff);
    CHECK(p != NULL);
    CHECK(strcmp(ff_profile(p), "iw4-pc") == 0);
    expect_rawfile(p, rname, payload);
    ff_close(p);
}

static void test_bad_magic(void) {
    const unsigned char junk[] = "IWffXXXX\x05\x00\x00\x00xxxx";
    CHECK(ff_open_mem(junk, sizeof(junk) - 1) == NULL);
    CHECK(ff_open_file("definitely_missing_hxfs_ff.ff") == NULL);
}

static void test_plugin(void) {
    const char payload[] = "mod";
    size_t zlen = 0, flen = 0;
    unsigned char* zone;
    unsigned char* ff;
    hxAssetPlugin* plug;
    unsigned char* buf = NULL;
    unsigned int sz = 0;
    const char* path = "hxfs_ff_test.ff";

    zone = build_zone_iw3("english/localizedstrings/mod.str", payload, 3, 0, &zlen);
    CHECK(zone != NULL);
    ff = wrap_zlib_ff(zone, zlen, 5, 0, 0, "IWffu100", &flen);
    free(zone);
    CHECK(ff != NULL);
    CHECK(write_file(path, ff, flen));
    free(ff);

    plug = helix_asset_plugin_create();
    CHECK(plug != NULL);
    CHECK(plug->format_name && strcmp(plug->format_name, "ff") == 0);
    CHECK(plug->open(plug, path) == 1);
    CHECK(plug->contains(plug, "zone.bin") == 1);
    CHECK(plug->contains(plug, "ff.profile") == 1);
    CHECK(plug->read(plug, "english/localizedstrings/mod.str", &buf, &sz) == 1);
    CHECK(sz == 3 && buf && memcmp(buf, "mod", 3) == 0);
    plug->free_buf(plug, buf);
    plug->close(plug);
    plug->destroy(plug);
    remove(path);
}

int main(void) {
    test_iw3_pc();
    test_iw3_xenon();
    test_waw_pc();
    test_iw4_pc();
    test_iw4_inner_zlib();
    test_iw4_authed();
    test_bad_magic();
    test_plugin();
    if (g_fails) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("hxfs_ff_test: ok\n");
    return 0;
}
