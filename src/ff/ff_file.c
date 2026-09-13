/*
 * hxfs_ff — versioned CoD FastFile reader (published IWff layouts).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Version table (LE or BE at offset 8) selects engine, extra header,
 * decompressor and rawfile record size. Auth wrappers (IWffs100) are
 * skipped, hashes/RSA are not verified. Inner IW4 rawfiles may be zlib.
 */

#include "ff_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define FF_VNAME_MAX 256
#define FF_MAX_UNCOMP (256u * 1024u * 1024u)
#define FF_MAX_RAWFILE (16u * 1024u * 1024u)
#define FF_AUTH_IW3 0x4000u
#define FF_CHUNK 0x2000u
#define FF_AUTH_GROUP_DATA (256u * FF_CHUNK)

enum FfPack {
    FF_PACK_ZLIB = 0,
    FF_PACK_BLOCKS = 1
};

enum FfRaw {
    FF_RAW_IW3 = 12,
    FF_RAW_IW4 = 16
};

typedef struct {
    const char* name;
    unsigned int version;
    int be;
    unsigned int extra; /* bytes after the 12-byte IWff header */
    enum FfPack pack;
    enum FfRaw raw;
} FfProfile;

static const FfProfile k_profiles[] = {
    /* CoD4 IW3 */
    {"iw3-pc", 0x5u, 0, 0, FF_PACK_ZLIB, FF_RAW_IW3},
    {"iw3-xenon", 0x1u, 1, 0, FF_PACK_BLOCKS, FF_RAW_IW3},
    {"iw3-ps3", 0x1u, 1, 0, FF_PACK_BLOCKS, FF_RAW_IW3},
    {"iw3-wii", 0x1A2u, 1, 0, FF_PACK_ZLIB, FF_RAW_IW3},
    /* WaW */
    {"waw-pc", 0x183u, 0, 0, FF_PACK_ZLIB, FF_RAW_IW3},
    {"waw-xenon", 0x183u, 1, 0, FF_PACK_BLOCKS, FF_RAW_IW3},
    {"waw-wii", 0x19Bu, 1, 0, FF_PACK_ZLIB, FF_RAW_IW3},
    /* MW2 / IW4x */
    {"iw4-pc", 0x114u, 0, 9, FF_PACK_ZLIB, FF_RAW_IW4},
    {"iw4-xenon", 0x10Du, 1, 25, FF_PACK_ZLIB, FF_RAW_IW4},
    {"iw4-ps3", 0x10Du, 1, 25, FF_PACK_BLOCKS, FF_RAW_IW4},
};

static const FfProfile k_unknown = {"unknown", 0, 0, 0, FF_PACK_ZLIB, FF_RAW_IW3};

typedef struct {
    char vname[FF_VNAME_MAX];
    unsigned int offset;
    unsigned int size;
    unsigned char* owned;
} FfEntry;

struct FfFile {
    unsigned char* zone;
    size_t zone_len;
    FfEntry* files;
    int count;
    int cap;
    const FfProfile* prof;
    unsigned int version;
    int be;
};

static unsigned int ff_rd_u32_le(const unsigned char* p) {
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

static unsigned int ff_rd_u32_be(const unsigned char* p) {
    return ((unsigned int)p[0] << 24)
         | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] << 8)
         | (unsigned int)p[3];
}

static unsigned int ff_rd_u32(const unsigned char* p, int be) {
    return be ? ff_rd_u32_be(p) : ff_rd_u32_le(p);
}

static const FfProfile* ff_match_profile(unsigned int le, unsigned int be, int* out_be) {
    size_t i;
    /* Prefer an unambiguous endian match. Xenon/PS3 share version 1 BE. */
    for (i = 0; i < sizeof(k_profiles) / sizeof(k_profiles[0]); i++) {
        const FfProfile* pr = &k_profiles[i];
        if (pr->be && be == pr->version) {
            *out_be = 1;
            return pr;
        }
        if (!pr->be && le == pr->version) {
            *out_be = 0;
            return pr;
        }
    }
    *out_be = 0;
    return &k_unknown;
}

static int ff_ascii_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static void ff_normalize(char* dst, size_t dstsz, const char* src) {
    size_t o = 0;
    if (!dstsz) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (*src == '/' || *src == '\\') src++;
    while (*src && o + 1 < dstsz) {
        char c = *src++;
        if (c == '\\') c = '/';
        dst[o++] = (char)tolower((unsigned char)c);
    }
    dst[o] = 0;
}

static int ff_ok_name(const char* s) {
    size_t n = 0;
    int slash = 0;
    int dot = 0;
    if (!s || !s[0]) return 0;
    for (; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (n >= FF_VNAME_MAX - 1) return 0;
        if (c == '/' || c == '\\') slash = 1;
        else if (c == '.') dot = 1;
        else if (!(isalnum(c) || c == '_' || c == '-' || c == '+')) return 0;
    }
    return n >= 3 && (slash || dot);
}

static int ff_find_last(const FfFile* p, const char* name) {
    char key[FF_VNAME_MAX];
    int i;
    if (!p || !name) return -1;
    ff_normalize(key, sizeof(key), name);
    if (!key[0]) return -1;
    for (i = p->count - 1; i >= 0; i--) {
        if (ff_ascii_eq(p->files[i].vname, key)) return i;
    }
    return -1;
}

static int ff_is_last_occurrence(const FfFile* p, int idx) {
    int j;
    for (j = idx + 1; j < p->count; j++) {
        if (ff_ascii_eq(p->files[j].vname, p->files[idx].vname)) return 0;
    }
    return 1;
}

static int ff_add(FfFile* p, const char* name, unsigned int offset, unsigned int size, unsigned char* owned) {
    char key[FF_VNAME_MAX];
    FfEntry* E;
    ff_normalize(key, sizeof(key), name);
    if (!key[0]) {
        free(owned);
        return 0;
    }
    if (p->count == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 8;
        FfEntry* n = (FfEntry*)realloc(p->files, (size_t)ncap * sizeof(FfEntry));
        if (!n) {
            free(owned);
            return 0;
        }
        p->files = n;
        p->cap = ncap;
    }
    E = &p->files[p->count++];
    memset(E, 0, sizeof(*E));
    memcpy(E->vname, key, sizeof(E->vname));
    E->offset = offset;
    E->size = size;
    E->owned = owned;
    return 1;
}

static int ff_looks_zlib(const unsigned char* src, size_t slen) {
    if (slen < 2) return 0;
    return src[0] == 0x78 && (src[1] == 0x01 || src[1] == 0x5E || src[1] == 0x9C || src[1] == 0xDA);
}

static int ff_inflate_zlib(const unsigned char* src, size_t slen, unsigned char** out, size_t* olen) {
    z_stream s;
    unsigned char* buf;
    size_t cap;
    int r;
    if (slen > 0xFFFFFFFFu) return 0;
    cap = slen * 4u + 65536u;
    if (cap < 65536u) cap = 65536u;
    if (cap > FF_MAX_UNCOMP) cap = FF_MAX_UNCOMP;
    buf = (unsigned char*)malloc(cap);
    if (!buf) return 0;
    memset(&s, 0, sizeof(s));
    s.next_in = (Bytef*)src;
    s.avail_in = (uInt)slen;
    s.next_out = buf;
    s.avail_out = (uInt)cap;
    if (inflateInit(&s) != Z_OK) {
        free(buf);
        return 0;
    }
    for (;;) {
        r = inflate(&s, Z_NO_FLUSH);
        if (r == Z_STREAM_END) break;
        if (r == Z_OK || r == Z_BUF_ERROR) {
            size_t used = (size_t)s.total_out;
            size_t ncap;
            unsigned char* nbuf;
            if (s.avail_out != 0) {
                inflateEnd(&s);
                free(buf);
                return 0;
            }
            ncap = cap * 2u;
            if (ncap > FF_MAX_UNCOMP) ncap = FF_MAX_UNCOMP;
            if (ncap <= cap) {
                inflateEnd(&s);
                free(buf);
                return 0;
            }
            nbuf = (unsigned char*)realloc(buf, ncap);
            if (!nbuf) {
                inflateEnd(&s);
                free(buf);
                return 0;
            }
            buf = nbuf;
            s.next_out = buf + used;
            s.avail_out = (uInt)(ncap - used);
            cap = ncap;
            continue;
        }
        inflateEnd(&s);
        free(buf);
        return 0;
    }
    inflateEnd(&s);
    *out = buf;
    *olen = (size_t)s.total_out;
    return 1;
}

static int ff_inflate_raw_once(const unsigned char* src, unsigned int slen, unsigned char* dst, unsigned int dcap, unsigned int* dout) {
    z_stream s;
    int r;
    memset(&s, 0, sizeof(s));
    s.next_in = (Bytef*)src;
    s.avail_in = slen;
    s.next_out = dst;
    s.avail_out = dcap;
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return 0;
    r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END) return 0;
    *dout = (unsigned int)s.total_out;
    return 1;
}

static int ff_inflate_blocks(const unsigned char* src, size_t slen, unsigned char** out, size_t* olen) {
    unsigned char* buf;
    size_t cap = 65536u;
    size_t used = 0;
    size_t pos = 0;
    int any = 0;
    buf = (unsigned char*)malloc(cap);
    if (!buf) return 0;
    while (pos + 2 <= slen) {
        unsigned int clen = ((unsigned int)src[pos] << 8) | (unsigned int)src[pos + 1];
        unsigned int got = 0;
        unsigned char tmp[65536];
        pos += 2;
        if (clen == 1) break;
        if (clen == 0) continue;
        if (pos + clen > slen) {
            free(buf);
            return 0;
        }
        if (!ff_inflate_raw_once(src + pos, clen, tmp, 65536u, &got)) {
            free(buf);
            return 0;
        }
        if (used + got > cap) {
            size_t ncap = cap * 2u;
            unsigned char* nbuf;
            while (ncap < used + got) ncap *= 2u;
            if (ncap > FF_MAX_UNCOMP) {
                free(buf);
                return 0;
            }
            nbuf = (unsigned char*)realloc(buf, ncap);
            if (!nbuf) {
                free(buf);
                return 0;
            }
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + used, tmp, got);
        used += got;
        pos += clen;
        any = 1;
    }
    if (!any) {
        free(buf);
        return 0;
    }
    *out = buf;
    *olen = used;
    return 1;
}

/* Skip IW4 DB_AuthHeader chunk, then skip per-group hash chunks (IW4x / 360). */
static int ff_unwrap_iw4_authed(const unsigned char* src, size_t slen, unsigned char** out, size_t* olen) {
    unsigned char* buf;
    size_t cap;
    size_t used = 0;
    size_t pos;
    if (slen < FF_CHUNK) return 0;
    pos = FF_CHUNK;
    cap = slen;
    buf = (unsigned char*)malloc(cap ? cap : 1);
    if (!buf) return 0;
    while (pos < slen) {
        size_t remain = slen - pos;
        size_t data;
        if (remain <= FF_CHUNK) break;
        pos += FF_CHUNK;
        remain = slen - pos;
        data = remain > FF_AUTH_GROUP_DATA ? FF_AUTH_GROUP_DATA : remain;
        if (used + data > cap) {
            size_t ncap = used + data;
            unsigned char* nbuf = (unsigned char*)realloc(buf, ncap);
            if (!nbuf) {
                free(buf);
                return 0;
            }
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + used, src + pos, data);
        used += data;
        pos += data;
    }
    if (!used) {
        free(buf);
        return 0;
    }
    *out = buf;
    *olen = used;
    return 1;
}

static int ff_decompress_payload(const FfProfile* pr, const unsigned char* payload, size_t plen, unsigned char** zone, size_t* zlen) {
    unsigned char* unwrapped = NULL;
    size_t ulen = 0;
    int ok = 0;
    const unsigned char* src = payload;
    size_t slen = plen;

    if (slen >= 8 && memcmp(src, "IWffs100", 8) == 0) {
        if (pr->raw == FF_RAW_IW4) {
            if (!ff_unwrap_iw4_authed(src, slen, &unwrapped, &ulen)) return 0;
            src = unwrapped;
            slen = ulen;
            ok = ff_inflate_zlib(src, slen, zone, zlen);
            free(unwrapped);
            return ok;
        }
        if (slen < FF_AUTH_IW3) return 0;
        src += FF_AUTH_IW3;
        slen -= FF_AUTH_IW3;
    }

    if (pr->pack == FF_PACK_BLOCKS) {
        if (ff_looks_zlib(src, slen)) ok = ff_inflate_zlib(src, slen, zone, zlen);
        if (!ok) ok = ff_inflate_blocks(src, slen, zone, zlen);
        if (!ok) ok = ff_inflate_zlib(src, slen, zone, zlen);
        return ok;
    }

    if (ff_looks_zlib(src, slen)) ok = ff_inflate_zlib(src, slen, zone, zlen);
    if (!ok) ok = ff_inflate_blocks(src, slen, zone, zlen);
    if (!ok) ok = ff_inflate_zlib(src, slen, zone, zlen);
    return ok;
}

static int ff_take_name(const unsigned char* zone, size_t zone_len, size_t name_ofs, const char** name, size_t* nlen) {
    size_t n = 0;
    if (name_ofs >= zone_len) return 0;
    *name = (const char*)(zone + name_ofs);
    while (name_ofs + n < zone_len && (*name)[n]) n++;
    if (n == 0 || name_ofs + n >= zone_len) return 0;
    if ((*name)[n] != 0) return 0;
    if (!ff_ok_name(*name)) return 0;
    *nlen = n;
    return 1;
}

static void ff_scan_rawfiles(FfFile* p) {
    size_t i;
    int be;
    if (!p->zone || p->zone_len < 16 || !p->prof) return;
    be = p->be;
    for (i = 0; i + 16 <= p->zone_len; i++) {
        const char* name = NULL;
        size_t nlen = 0;
        size_t data_ofs;
        unsigned int p0, p1, p2, p3;

        p0 = ff_rd_u32(p->zone + i, be);
        if (p0 != 0xFFFFFFFFu) continue;

        if (p->prof->raw == FF_RAW_IW4) {
            unsigned int clen;
            unsigned int ulen;
            if (i + 20 > p->zone_len) continue;
            clen = ff_rd_u32(p->zone + i + 4, be);
            ulen = ff_rd_u32(p->zone + i + 8, be);
            p3 = ff_rd_u32(p->zone + i + 12, be);
            if (p3 != 0xFFFFFFFFu || ulen == 0 || ulen > FF_MAX_RAWFILE) continue;
            if (!ff_take_name(p->zone, p->zone_len, i + 16, &name, &nlen)) continue;
            data_ofs = i + 16 + nlen + 1;
            if (clen == 0) {
                if (data_ofs + ulen <= p->zone_len) {
                    ff_add(p, name, (unsigned int)data_ofs, ulen, NULL);
                    i = data_ofs + ulen - 1;
                }
            } else if (clen <= FF_MAX_RAWFILE && data_ofs + clen <= p->zone_len) {
                unsigned char* plain = NULL;
                size_t plen = 0;
                if (ff_inflate_zlib(p->zone + data_ofs, clen, &plain, &plen) && plen == ulen) {
                    ff_add(p, name, 0, ulen, plain);
                    i = data_ofs + clen - 1;
                } else {
                    free(plain);
                }
            }
            continue;
        }

        p1 = ff_rd_u32(p->zone + i + 4, be);
        p2 = (i + 12 <= p->zone_len) ? ff_rd_u32(p->zone + i + 8, be) : 0;
        if (p2 == 0xFFFFFFFFu && p1 > 0 && p1 <= FF_MAX_RAWFILE && ff_take_name(p->zone, p->zone_len, i + 12, &name, &nlen)) {
            data_ofs = i + 12 + nlen + 1;
            if (data_ofs + p1 <= p->zone_len) {
                ff_add(p, name, (unsigned int)data_ofs, p1, NULL);
                i = data_ofs + p1 - 1;
            }
        }
    }
}

static FfFile* ff_fail(FfFile* p) {
    ff_close(p);
    return NULL;
}

static FfFile* ff_parse_owned(unsigned char* bytes, size_t len) {
    FfFile* p;
    const unsigned char* payload;
    size_t plen;
    unsigned char* zone = NULL;
    size_t zlen = 0;
    unsigned int le, be_v;
    int matched_be = 0;
    const FfProfile* pr;
    char ident[96];
    unsigned char* ident_copy;
    size_t ident_len;

    p = (FfFile*)calloc(1, sizeof(FfFile));
    if (!p) {
        free(bytes);
        return NULL;
    }
    if (len < 12) {
        free(bytes);
        free(p);
        return NULL;
    }
    if (memcmp(bytes, "IWffu100", 8) != 0 && memcmp(bytes, "IWff0100", 8) != 0) {
        free(bytes);
        free(p);
        return NULL;
    }

    le = ff_rd_u32_le(bytes + 8);
    be_v = ff_rd_u32_be(bytes + 8);
    pr = ff_match_profile(le, be_v, &matched_be);
    p->prof = pr;
    p->be = matched_be;
    p->version = matched_be ? be_v : le;

    if (12u + pr->extra > len) {
        free(bytes);
        free(p);
        return NULL;
    }
    payload = bytes + 12 + pr->extra;
    plen = len - 12 - pr->extra;

    if (!ff_decompress_payload(pr, payload, plen, &zone, &zlen)) {
        free(bytes);
        free(p);
        return NULL;
    }
    free(bytes);
    if (!zone || zlen == 0) {
        free(zone);
        free(p);
        return NULL;
    }

    p->zone = zone;
    p->zone_len = zlen;
    if (!ff_add(p, "zone.bin", 0, (unsigned int)zlen, NULL)) return ff_fail(p);

    ident_len = (size_t)snprintf(ident, sizeof(ident), "%s 0x%x %s\n", pr->name, p->version, p->be ? "be" : "le");
    ident_copy = (unsigned char*)malloc(ident_len + 1);
    if (!ident_copy) return ff_fail(p);
    memcpy(ident_copy, ident, ident_len + 1);
    if (!ff_add(p, "ff.profile", 0, (unsigned int)ident_len, ident_copy)) return ff_fail(p);

    ff_scan_rawfiles(p);
    return p;
}

FfFile* ff_open_mem(const void* bytes, size_t len) {
    unsigned char* copy;
    if (!bytes && len != 0) return NULL;
    copy = (unsigned char*)malloc(len ? len : 1);
    if (!copy) return NULL;
    if (len && bytes) memcpy(copy, bytes, len);
    return ff_parse_owned(copy, len);
}

FfFile* ff_open_file(const char* path) {
    FILE* f;
    long sz;
    unsigned char* buf;
    size_t n;

    if (!path || !path[0]) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char*)malloc(sz > 0 ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }
    return ff_parse_owned(buf, (size_t)sz);
}

void ff_close(FfFile* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->count; i++) free(p->files[i].owned);
    free(p->zone);
    free(p->files);
    free(p);
}

int ff_contains(const FfFile* p, const char* name) {
    return ff_find_last(p, name) >= 0;
}

int ff_read(const FfFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int idx;
    unsigned char* buf;
    const FfEntry* E;
    const unsigned char* src;
    if (!p || !out_buf || !out_size) return 0;
    idx = ff_find_last(p, name);
    if (idx < 0) return 0;
    E = &p->files[idx];
    src = E->owned ? E->owned : (p->zone + E->offset);
    if (!E->owned && (size_t)E->offset + E->size > p->zone_len) return 0;
    buf = (unsigned char*)malloc(E->size ? E->size : 1);
    if (!buf) return 0;
    if (E->size) memcpy(buf, src, E->size);
    *out_buf = buf;
    *out_size = E->size;
    return 1;
}

void ff_list(const FfFile* p, char*** names, unsigned int* count) {
    char** arr;
    unsigned int n = 0;
    unsigned int k = 0;
    int i;
    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        if (ff_is_last_occurrence(p, i)) n++;
    }
    arr = (char**)malloc(n ? n * sizeof(char*) : sizeof(char*));
    if (!arr) return;
    for (i = 0; i < p->count; i++) {
        size_t len;
        if (!ff_is_last_occurrence(p, i)) continue;
        len = strlen(p->files[i].vname);
        arr[k] = (char*)malloc(len + 1);
        if (!arr[k]) {
            ff_free_list(arr, k);
            return;
        }
        memcpy(arr[k], p->files[i].vname, len + 1);
        k++;
    }
    *names = arr;
    *count = k;
}

void ff_free_list(char** names, unsigned int count) {
    unsigned int i;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

const char* ff_profile(const FfFile* p) {
    return (p && p->prof) ? p->prof->name : "";
}

unsigned int ff_version(const FfFile* p) {
    return p ? p->version : 0;
}

int ff_big_endian(const FfFile* p) {
    return p ? p->be : 0;
}
