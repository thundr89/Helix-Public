/*
 * Shared ZIP reader for PK3 / PK4 / IWD (published APPNOTE).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Central directory + local headers. Method 0 (store) and 8 (deflate).
 * Inflate via zlib raw deflate (-MAX_WBITS). Not derived from ioquake3 / dhewm3.
 */

#include "zip_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_VNAME_MAX 256
#define ZIP_MAX_FILES 65535u
#define ZIP_MAX_UNCOMP (256u * 1024u * 1024u)

#define ZIP_SIG_LOCAL 0x04034b50u
#define ZIP_SIG_CENTRAL 0x02014b50u
#define ZIP_SIG_EOCD 0x06054b50u

#define ZIP_METHOD_STORE 0
#define ZIP_METHOD_DEFLATE 8

typedef struct {
    char vname[ZIP_VNAME_MAX];
    unsigned int local_ofs;
    unsigned int data_ofs;
    unsigned int csize;
    unsigned int usize;
    unsigned int crc;
    unsigned int method;
} ZipEntry;

struct ZipFile {
    unsigned char* bytes;
    size_t length;
    FILE* fp;
    ZipEntry* files;
    int count;
    int* hash_table;
    unsigned int hash_mask;
};

static unsigned int zip_rd_u16(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int zip_rd_u32(const unsigned char* p) {
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

static int zip_ascii_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static unsigned int zip_hash_str(const char* s) {
    unsigned int h = 2166136261u;
    while (*s) {
        h = (h ^ (unsigned char)*s++) * 16777619u;
    }
    return h;
}

static void zip_normalize(char* dst, size_t dstsz, const char* src, size_t srclen) {
    size_t i = 0;
    size_t o = 0;
    if (!dstsz) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (i < srclen && (src[i] == '/' || src[i] == '\\')) i++;
    while (i + 1 < srclen && src[i] == '.' && (src[i + 1] == '/' || src[i + 1] == '\\')) i += 2;
    while (i < srclen && src[i] && o + 1 < dstsz) {
        char c = src[i++];
        if (c == '\\') c = '/';
        dst[o++] = (char)tolower((unsigned char)c);
    }
    dst[o] = 0;
}

static int zip_find_last(const ZipFile* p, const char* name) {
    char key[ZIP_VNAME_MAX];
    int i;
    if (!p || !name) return -1;
    zip_normalize(key, sizeof(key), name, strlen(name));
    if (!key[0]) return -1;
    if (p->hash_table && p->hash_mask) {
        unsigned int h = zip_hash_str(key) & p->hash_mask;
        while (p->hash_table[h] != -1) {
            int idx = p->hash_table[h];
            if (strcmp(p->files[idx].vname, key) == 0) return idx;
            h = (h + 1u) & p->hash_mask;
        }
        return -1;
    }
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_ascii_eq(p->files[i].vname, key)) return i;
    }
    return -1;
}

static int zip_is_last_occurrence(const ZipFile* p, int idx) {
    if (p->hash_table && p->hash_mask) {
        int last = zip_find_last(p, p->files[idx].vname);
        return last == idx;
    }
    int j;
    for (j = idx + 1; j < p->count; j++) {
        if (zip_ascii_eq(p->files[j].vname, p->files[idx].vname)) return 0;
    }
    return 1;
}

static ZipFile* zip_fail(ZipFile* p) {
    zip_close(p);
    return NULL;
}

static int zip_find_eocd(const unsigned char* bytes, size_t len, size_t* out_ofs) {
    size_t min_ofs;
    size_t i;
    if (len < 22) return 0;
    min_ofs = (len - 22 > 65535u) ? (len - 22 - 65535u) : 0;
    i = len - 22;
    for (;;) {
        if (zip_rd_u32(bytes + i) == ZIP_SIG_EOCD) {
            unsigned int comment = zip_rd_u16(bytes + i + 20);
            if (i + 22u + comment == len) {
                *out_ofs = i;
                return 1;
            }
        }
        if (i == min_ofs) break;
        i--;
    }
    return 0;
}

static int zip_eocd_in_tail(const unsigned char* tail, size_t tail_n, size_t file_n, size_t* abs_ofs);
static int zip_add_cd(ZipFile* p, const unsigned char* cd, unsigned int cd_size, unsigned int num);

static int zip_inflate_raw(const unsigned char* src, unsigned int src_len,
                           unsigned char* dst, unsigned int dst_len) {
    z_stream s;
    int r;
    memset(&s, 0, sizeof(s));
    s.next_in = (Bytef*)src;
    s.avail_in = src_len;
    s.next_out = dst;
    s.avail_out = dst_len;
    r = inflateInit2(&s, -MAX_WBITS);
    if (r != Z_OK) return 0;
    r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    return r == Z_STREAM_END && s.total_out == dst_len;
}

static ZipFile* zip_parse_owned(unsigned char* bytes, size_t len) {
    ZipFile* p;
    size_t eocd;
    unsigned int disk, start_disk, num_here, num, cd_size, cd_ofs;
    unsigned int i;
    size_t cur;
    int out;

    p = (ZipFile*)calloc(1, sizeof(ZipFile));
    if (!p) {
        free(bytes);
        return NULL;
    }
    p->bytes = bytes;
    p->length = len;

    if (!zip_find_eocd(bytes, len, &eocd)) return zip_fail(p);
    disk = zip_rd_u16(bytes + eocd + 4);
    start_disk = zip_rd_u16(bytes + eocd + 6);
    num_here = zip_rd_u16(bytes + eocd + 8);
    num = zip_rd_u16(bytes + eocd + 10);
    cd_size = zip_rd_u32(bytes + eocd + 12);
    cd_ofs = zip_rd_u32(bytes + eocd + 16);
    if (disk != 0 || start_disk != 0 || num_here != num) return zip_fail(p);
    if (num == 0xFFFFu || cd_ofs == 0xFFFFFFFFu || cd_size == 0xFFFFFFFFu) return zip_fail(p);
    if (num > ZIP_MAX_FILES) return zip_fail(p);
    if ((size_t)cd_ofs > len || (size_t)cd_size > len - (size_t)cd_ofs) return zip_fail(p);

    p->files = (ZipEntry*)calloc((size_t)num + 1u, sizeof(ZipEntry));
    if (num > 0 && !p->files) return zip_fail(p);
    out = 0;
    cur = (size_t)cd_ofs;

    for (i = 0; i < num; i++) {
        unsigned int flag, method, crc, csize, usize;
        unsigned int namelen, extralen, commentlen, local_ofs;
        unsigned int lname, lextra, data_ofs;
        const unsigned char* fname;
        ZipEntry* E;
        size_t nlen;

        if (cur + 46 > (size_t)cd_ofs + cd_size) return zip_fail(p);
        if (zip_rd_u32(bytes + cur) != ZIP_SIG_CENTRAL) return zip_fail(p);
        flag = zip_rd_u16(bytes + cur + 8);
        method = zip_rd_u16(bytes + cur + 10);
        crc = zip_rd_u32(bytes + cur + 16);
        csize = zip_rd_u32(bytes + cur + 20);
        usize = zip_rd_u32(bytes + cur + 24);
        namelen = zip_rd_u16(bytes + cur + 28);
        extralen = zip_rd_u16(bytes + cur + 30);
        commentlen = zip_rd_u16(bytes + cur + 32);
        local_ofs = zip_rd_u32(bytes + cur + 42);
        if (cur + 46 + namelen + extralen + commentlen > (size_t)cd_ofs + cd_size) return zip_fail(p);
        fname = bytes + cur + 46;
        cur += 46u + namelen + extralen + commentlen;

        if (flag & 1u) continue;
        if (method != ZIP_METHOD_STORE && method != ZIP_METHOD_DEFLATE) continue;
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu) continue;
        if (usize > ZIP_MAX_UNCOMP) continue;
        if (namelen == 0 || namelen >= ZIP_VNAME_MAX) continue;

        E = &p->files[out];
        memset(E, 0, sizeof(*E));
        zip_normalize(E->vname, sizeof(E->vname), (const char*)fname, namelen);
        nlen = strlen(E->vname);
        if (!nlen) continue;
        if (E->vname[nlen - 1] == '/') continue;

        if ((size_t)local_ofs + 30 > len) return zip_fail(p);
        if (zip_rd_u32(bytes + local_ofs) != ZIP_SIG_LOCAL) return zip_fail(p);
        lname = zip_rd_u16(bytes + local_ofs + 26);
        lextra = zip_rd_u16(bytes + local_ofs + 28);
        data_ofs = local_ofs + 30u + lname + lextra;
        if ((size_t)data_ofs > len || (size_t)csize > len - (size_t)data_ofs) return zip_fail(p);
        if (method == ZIP_METHOD_STORE && csize != usize) continue;

        E->data_ofs = data_ofs;
        E->csize = csize;
        E->usize = usize;
        E->crc = crc;
        E->method = method;
        out++;
    }

    p->count = out;
    if (out > 0) {
        unsigned int cap = 16;
        while (cap < (unsigned int)out * 2u) cap <<= 1;
        p->hash_mask = cap - 1u;
        p->hash_table = (int*)malloc(cap * sizeof(int));
        if (p->hash_table) {
            unsigned int k;
            for (k = 0; k < cap; k++) p->hash_table[k] = -1;
            for (i = 0; i < (unsigned int)out; i++) {
                unsigned int h = zip_hash_str(p->files[i].vname) & p->hash_mask;
                while (p->hash_table[h] != -1) {
                    if (strcmp(p->files[p->hash_table[h]].vname, p->files[i].vname) == 0) {
                        break;
                    }
                    h = (h + 1u) & p->hash_mask;
                }
                p->hash_table[h] = (int)i;
            }
        }
    }
    return p;
}

ZipFile* zip_open_mem(const void* bytes, size_t len) {
    unsigned char* copy;
    if (!bytes && len != 0) return NULL;
    copy = (unsigned char*)malloc(len ? len : 1);
    if (!copy) return NULL;
    if (len && bytes) memcpy(copy, bytes, len);
    return zip_parse_owned(copy, len);
}

ZipFile* zip_open_file(const char* path) {
    FILE* f;
    long sz;
    size_t tail_n, eocd;
    unsigned char* tail = NULL;
    unsigned char* cd = NULL;
    ZipFile* p;
    unsigned int disk, start_disk, num_here, num, cd_size, cd_ofs;

    if (!path || !path[0]) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 22) {
        fclose(f);
        return NULL;
    }
    tail_n = (size_t)sz < 65557u ? (size_t)sz : 65557u;
    if (fseek(f, (long)((size_t)sz - tail_n), SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    tail = (unsigned char*)malloc(tail_n);
    if (!tail || fread(tail, 1, tail_n, f) != tail_n || !zip_eocd_in_tail(tail, tail_n, (size_t)sz, &eocd)) {
        free(tail);
        fclose(f);
        return NULL;
    }
    disk = zip_rd_u16(tail + (eocd - ((size_t)sz - tail_n)) + 4);
    start_disk = zip_rd_u16(tail + (eocd - ((size_t)sz - tail_n)) + 6);
    num_here = zip_rd_u16(tail + (eocd - ((size_t)sz - tail_n)) + 8);
    num = zip_rd_u16(tail + (eocd - ((size_t)sz - tail_n)) + 10);
    cd_size = zip_rd_u32(tail + (eocd - ((size_t)sz - tail_n)) + 12);
    cd_ofs = zip_rd_u32(tail + (eocd - ((size_t)sz - tail_n)) + 16);
    free(tail);
    if (disk != 0 || start_disk != 0 || num_here != num || num == 0xFFFFu || cd_ofs == 0xFFFFFFFFu ||
        cd_size == 0xFFFFFFFFu || (size_t)cd_ofs + (size_t)cd_size > (size_t)sz) {
        fclose(f);
        return NULL;
    }
    cd = (unsigned char*)malloc(cd_size ? cd_size : 1);
    if (!cd || fseek(f, (long)cd_ofs, SEEK_SET) != 0 || (cd_size && fread(cd, 1, cd_size, f) != cd_size)) {
        free(cd);
        fclose(f);
        return NULL;
    }
    p = (ZipFile*)calloc(1, sizeof(ZipFile));
    if (!p || !zip_add_cd(p, cd, cd_size, num)) {
        free(cd);
        if (p) zip_close(p);
        fclose(f);
        return NULL;
    }
    free(cd);
    p->fp = f;
    p->length = (size_t)sz;
    return p;
}

static int zip_eocd_in_tail(const unsigned char* tail, size_t tail_n, size_t file_n, size_t* abs_ofs) {
    size_t base;
    size_t i;
    size_t min_i;
    if (!tail || tail_n < 22 || file_n < tail_n) return 0;
    base = file_n - tail_n;
    min_i = (tail_n - 22 > 65535u) ? (tail_n - 22 - 65535u) : 0;
    i = tail_n - 22;
    for (;;) {
        if (zip_rd_u32(tail + i) == ZIP_SIG_EOCD) {
            unsigned int comment = zip_rd_u16(tail + i + 20);
            if (base + i + 22u + comment == file_n) {
                *abs_ofs = base + i;
                return 1;
            }
        }
        if (i == min_i) break;
        i--;
    }
    return 0;
}

static void zip_index(ZipFile* p) {
    unsigned int cap = 16;
    unsigned int i;
    unsigned int k;
    if (p->count <= 0) return;
    while (cap < (unsigned int)p->count * 2u) cap <<= 1;
    p->hash_mask = cap - 1u;
    p->hash_table = (int*)malloc(cap * sizeof(int));
    if (!p->hash_table) return;
    for (k = 0; k < cap; k++) p->hash_table[k] = -1;
    for (i = 0; i < (unsigned int)p->count; i++) {
        unsigned int h = zip_hash_str(p->files[i].vname) & p->hash_mask;
        while (p->hash_table[h] != -1) {
            if (strcmp(p->files[p->hash_table[h]].vname, p->files[i].vname) == 0) break;
            h = (h + 1u) & p->hash_mask;
        }
        p->hash_table[h] = (int)i;
    }
}

/* Central directory only. Local headers are checked when a member is read. */
static int zip_add_cd(ZipFile* p, const unsigned char* cd, unsigned int cd_size, unsigned int num) {
    unsigned int i;
    size_t cur = 0;
    int out = 0;
    if (num > ZIP_MAX_FILES) return 0;
    p->files = (ZipEntry*)calloc((size_t)num + 1u, sizeof(ZipEntry));
    if (num > 0 && !p->files) return 0;
    for (i = 0; i < num; i++) {
        unsigned int flag, method, crc, csize, usize;
        unsigned int namelen, extralen, commentlen, local_ofs;
        const unsigned char* fname;
        ZipEntry* E;
        size_t nlen;
        if (cur + 46 > cd_size) return 0;
        if (zip_rd_u32(cd + cur) != ZIP_SIG_CENTRAL) return 0;
        flag = zip_rd_u16(cd + cur + 8);
        method = zip_rd_u16(cd + cur + 10);
        crc = zip_rd_u32(cd + cur + 16);
        csize = zip_rd_u32(cd + cur + 20);
        usize = zip_rd_u32(cd + cur + 24);
        namelen = zip_rd_u16(cd + cur + 28);
        extralen = zip_rd_u16(cd + cur + 30);
        commentlen = zip_rd_u16(cd + cur + 32);
        local_ofs = zip_rd_u32(cd + cur + 42);
        if (cur + 46 + namelen + extralen + commentlen > cd_size) return 0;
        fname = cd + cur + 46;
        cur += 46u + namelen + extralen + commentlen;
        if (flag & 1u) continue;
        if (method != ZIP_METHOD_STORE && method != ZIP_METHOD_DEFLATE) continue;
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || local_ofs == 0xFFFFFFFFu) continue;
        if (usize > ZIP_MAX_UNCOMP) continue;
        if (namelen == 0 || namelen >= ZIP_VNAME_MAX) continue;
        if (method == ZIP_METHOD_STORE && csize != usize) continue;
        E = &p->files[out];
        memset(E, 0, sizeof(*E));
        zip_normalize(E->vname, sizeof(E->vname), (const char*)fname, namelen);
        nlen = strlen(E->vname);
        if (!nlen || E->vname[nlen - 1] == '/') continue;
        E->local_ofs = local_ofs;
        E->csize = csize;
        E->usize = usize;
        E->crc = crc;
        E->method = method;
        out++;
    }
    p->count = out;
    zip_index(p);
    return 1;
}

static int zip_load_comp(const ZipFile* p, const ZipEntry* E, unsigned char** out) {
    unsigned char hdr[30];
    unsigned int namelen, extralen;
    unsigned char* buf;
    if (!p->fp || !E || !out) return 0;
    if (fseek(p->fp, (long)E->local_ofs, SEEK_SET) != 0) return 0;
    if (fread(hdr, 1, 30, p->fp) != 30) return 0;
    if (zip_rd_u32(hdr) != ZIP_SIG_LOCAL) return 0;
    namelen = zip_rd_u16(hdr + 26);
    extralen = zip_rd_u16(hdr + 28);
    if (fseek(p->fp, (long)namelen + (long)extralen, SEEK_CUR) != 0) return 0;
    buf = (unsigned char*)malloc(E->csize ? E->csize : 1);
    if (!buf) return 0;
    if (E->csize && fread(buf, 1, E->csize, p->fp) != E->csize) {
        free(buf);
        return 0;
    }
    *out = buf;
    return 1;
}

void zip_close(ZipFile* p) {
    if (!p) return;
    if (p->fp) fclose(p->fp);
    free(p->hash_table);
    free(p->bytes);
    free(p->files);
    free(p);
}

int zip_contains(const ZipFile* p, const char* name) {
    return zip_find_last(p, name) >= 0;
}

int zip_checksum(const ZipFile* p, const char* name, unsigned int* crc) {
    int idx;
    if (!p || !crc) return 0;
    idx = zip_find_last(p, name);
    if (idx < 0) return 0;
    *crc = p->files[idx].crc;
    return 1;
}

int zip_read(const ZipFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int idx;
    unsigned char* buf;
    unsigned char* comp = NULL;
    const ZipEntry* E;
    const unsigned char* src;
    if (!p || !out_buf || !out_size) return 0;
    idx = zip_find_last(p, name);
    if (idx < 0) return 0;
    E = &p->files[idx];
    buf = (unsigned char*)malloc(E->usize ? E->usize : 1);
    if (!buf) return 0;
    if (p->fp) {
        if (!zip_load_comp(p, E, &comp)) {
            free(buf);
            return 0;
        }
        src = comp;
    } else {
        src = p->bytes + E->data_ofs;
    }
    if (E->method == ZIP_METHOD_STORE) {
        if (E->usize) memcpy(buf, src, E->usize);
    } else if (!zip_inflate_raw(src, E->csize, buf, E->usize)) {
        free(comp);
        free(buf);
        return 0;
    }
    free(comp);
    if (crc32(0L, buf, E->usize) != E->crc) {
        free(buf);
        return 0;
    }
    *out_buf = buf;
    *out_size = E->usize;
    return 1;
}

void zip_list(const ZipFile* p, char*** names, unsigned int* count) {
    char** arr;
    unsigned int n = 0;
    unsigned int k = 0;
    int i;
    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        if (zip_is_last_occurrence(p, i)) n++;
    }
    arr = (char**)malloc(n ? n * sizeof(char*) : sizeof(char*));
    if (!arr) return;
    for (i = 0; i < p->count; i++) {
        size_t len;
        if (!zip_is_last_occurrence(p, i)) continue;
        len = strlen(p->files[i].vname);
        arr[k] = (char*)malloc(len + 1);
        if (!arr[k]) {
            zip_free_list(arr, k);
            return;
        }
        memcpy(arr[k], p->files[i].vname, len + 1);
        k++;
    }
    *names = arr;
    *count = k;
}

void zip_free_list(char** names, unsigned int count) {
    unsigned int i;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}
