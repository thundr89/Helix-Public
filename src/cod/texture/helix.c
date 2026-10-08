/*
 * CoD image bytes to a PNG the Helix material table can load with stb.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "texture/helix.h"
#include "texture/read.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static unsigned long png_crc(const unsigned char* data, size_t n) {
    return crc32(0L, data, (uInt)n);
}

static int be32(unsigned char* d, unsigned long v) {
    d[0] = (unsigned char)((v >> 24) & 255);
    d[1] = (unsigned char)((v >> 16) & 255);
    d[2] = (unsigned char)((v >> 8) & 255);
    d[3] = (unsigned char)(v & 255);
    return 4;
}

static int append(unsigned char** buf, size_t* len, size_t* cap, const void* src, size_t n) {
    if (*len + n > *cap) {
        size_t ncap = *cap ? *cap * 2 : 256;
        unsigned char* grown;
        while (*len + n > ncap) ncap *= 2;
        grown = (unsigned char*)realloc(*buf, ncap);
        if (!grown) return 0;
        *buf = grown;
        *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return 1;
}

static int chunk(unsigned char** buf, size_t* len, size_t* cap, const char type[4],
                 const unsigned char* data, size_t n) {
    unsigned char hdr[8];
    unsigned char crc_bytes[4];
    unsigned char* crc_src;
    unsigned long c;
    be32(hdr, (unsigned long)n);
    memcpy(hdr + 4, type, 4);
    if (!append(buf, len, cap, hdr, 8)) return 0;
    if (n && !append(buf, len, cap, data, n)) return 0;
    crc_src = (unsigned char*)malloc(4 + n);
    if (!crc_src) return 0;
    memcpy(crc_src, type, 4);
    if (n) memcpy(crc_src + 4, data, n);
    c = png_crc(crc_src, 4 + n);
    free(crc_src);
    be32(crc_bytes, c);
    return append(buf, len, cap, crc_bytes, 4);
}

int texture_to_png(const void* data, size_t size, unsigned char** out_buf, unsigned int* out_size) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char* rgba = NULL;
    unsigned char* raw = NULL;
    unsigned char* comp = NULL;
    unsigned char* png = NULL;
    unsigned char ihdr[13];
    int w = 0, h = 0, y;
    size_t raw_n, png_len = 0, png_cap = 0;
    uLongf comp_n;
    const unsigned char* p = (const unsigned char*)data;
    if (!out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;
    if (data && size >= 8 && memcmp(p, sig, 4) == 0 && p[4] == 13) {
        unsigned char* copy = (unsigned char*)malloc(size);
        if (!copy) return 0;
        memcpy(copy, data, size);
        *out_buf = copy;
        *out_size = (unsigned int)size;
        return 1;
    }
    if (!texture_read_rgba(data, size, &rgba, &w, &h)) return 0;
    raw_n = (size_t)h * ((size_t)w * 4u + 1u);
    raw = (unsigned char*)malloc(raw_n);
    if (!raw) {
        free(rgba);
        return 0;
    }
    for (y = 0; y < h; y++) {
        unsigned char* row = raw + (size_t)y * ((size_t)w * 4u + 1u);
        row[0] = 0;
        memcpy(row + 1, rgba + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    }
    free(rgba);
    comp_n = compressBound((uLong)raw_n);
    comp = (unsigned char*)malloc(comp_n);
    if (!comp || compress(comp, &comp_n, raw, (uLong)raw_n) != Z_OK) {
        free(raw);
        free(comp);
        return 0;
    }
    free(raw);
    memset(ihdr, 0, sizeof(ihdr));
    be32(ihdr, (unsigned long)w);
    be32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    if (!append(&png, &png_len, &png_cap, sig, 8) ||
        !chunk(&png, &png_len, &png_cap, "IHDR", ihdr, 13) ||
        !chunk(&png, &png_len, &png_cap, "IDAT", comp, (size_t)comp_n) ||
        !chunk(&png, &png_len, &png_cap, "IEND", NULL, 0)) {
        free(comp);
        free(png);
        return 0;
    }
    free(comp);
    *out_buf = png;
    *out_size = (unsigned int)png_len;
    return 1;
}
