/*
 * CoD1 image bytes to RGBA. TGA (type 2 and 10) and DDS (uncompressed, DXT1, DXT3, DXT5).
 * An unknown compression returns 0 and does not invent pixels.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "texture/read.h"

#include <stdlib.h>
#include <string.h>

static int dims_ok(int w, int h) {
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return 0;
    if ((size_t)w * (size_t)h > 4096u * 4096u) return 0;
    return 1;
}

static unsigned char* alloc_rgba(int w, int h) {
    size_t n = (size_t)w * (size_t)h * 4u;
    return (unsigned char*)malloc(n);
}

static void put_px(unsigned char* rgba, int w, int h, int x, int y,
                   unsigned char r, unsigned char g, unsigned char b, unsigned char a, int top) {
    int row = top ? y : (h - 1 - y);
    unsigned char* p;
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    p = rgba + ((size_t)row * (size_t)w + (size_t)x) * 4u;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

static unsigned short ru16(const unsigned char* p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

static int read_tga(const unsigned char* p, size_t size, unsigned char** out, int* w, int* h) {
    int idlen, type, width, height, bpp, top, x, y, i;
    size_t off;
    unsigned char* rgba;
    if (size < 18) return 0;
    idlen = p[0];
    if (p[1] != 0) return 0; /* color-mapped images are not used */
    type = p[2];
    if (type != 2 && type != 10) return 0;
    width = (int)ru16(p + 12);
    height = (int)ru16(p + 14);
    bpp = p[16];
    if (bpp != 24 && bpp != 32) return 0;
    if (!dims_ok(width, height)) return 0;
    top = (p[17] & 0x20) != 0;
    off = 18u + (size_t)idlen;
    if (off > size) return 0;
    rgba = alloc_rgba(width, height);
    if (!rgba) return 0;
    if (type == 2) {
        size_t need = (size_t)width * (size_t)height * (bpp == 32 ? 4u : 3u);
        if (off + need > size) {
            free(rgba);
            return 0;
        }
        i = 0;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++, i++) {
                const unsigned char* px = p + off + (size_t)i * (bpp == 32 ? 4u : 3u);
                unsigned char a = bpp == 32 ? px[3] : 255;
                put_px(rgba, width, height, x, y, px[2], px[1], px[0], a, top);
            }
        }
    } else {
        int px_count = width * height;
        int filled = 0;
        int bytes = bpp == 32 ? 4 : 3;
        while (filled < px_count) {
            unsigned char packet;
            int count, raw, k;
            if (off >= size) {
                free(rgba);
                return 0;
            }
            packet = p[off++];
            count = (packet & 0x7f) + 1;
            raw = (packet & 0x80) == 0;
            if (filled + count > px_count) {
                free(rgba);
                return 0;
            }
            if (raw) {
                if (off + (size_t)count * (size_t)bytes > size) {
                    free(rgba);
                    return 0;
                }
                for (k = 0; k < count; k++, filled++) {
                    const unsigned char* px = p + off;
                    off += (size_t)bytes;
                    put_px(rgba, width, height, filled % width, filled / width,
                           px[2], px[1], px[0], bytes == 4 ? px[3] : 255, top);
                }
            } else {
                unsigned char px[4];
                if (off + (size_t)bytes > size) {
                    free(rgba);
                    return 0;
                }
                memcpy(px, p + off, (size_t)bytes);
                off += (size_t)bytes;
                for (k = 0; k < count; k++, filled++) {
                    put_px(rgba, width, height, filled % width, filled / width,
                           px[2], px[1], px[0], bytes == 4 ? px[3] : 255, top);
                }
            }
        }
    }
    *out = rgba;
    *w = width;
    *h = height;
    return 1;
}

static void rgb565(unsigned short c, unsigned char* r, unsigned char* g, unsigned char* b) {
    *r = (unsigned char)(((c >> 11) & 31) * 255 / 31);
    *g = (unsigned char)(((c >> 5) & 63) * 255 / 63);
    *b = (unsigned char)((c & 31) * 255 / 31);
}

static void dxt_colors(unsigned short c0, unsigned short c1, int four, unsigned char col[4][4]) {
    rgb565(c0, &col[0][0], &col[0][1], &col[0][2]);
    col[0][3] = 255;
    rgb565(c1, &col[1][0], &col[1][1], &col[1][2]);
    col[1][3] = 255;
    if (four || c0 > c1) {
        col[2][0] = (unsigned char)((2 * col[0][0] + col[1][0]) / 3);
        col[2][1] = (unsigned char)((2 * col[0][1] + col[1][1]) / 3);
        col[2][2] = (unsigned char)((2 * col[0][2] + col[1][2]) / 3);
        col[2][3] = 255;
        col[3][0] = (unsigned char)((col[0][0] + 2 * col[1][0]) / 3);
        col[3][1] = (unsigned char)((col[0][1] + 2 * col[1][1]) / 3);
        col[3][2] = (unsigned char)((col[0][2] + 2 * col[1][2]) / 3);
        col[3][3] = 255;
    } else {
        col[2][0] = (unsigned char)((col[0][0] + col[1][0]) / 2);
        col[2][1] = (unsigned char)((col[0][1] + col[1][1]) / 2);
        col[2][2] = (unsigned char)((col[0][2] + col[1][2]) / 2);
        col[2][3] = 255;
        col[3][0] = col[3][1] = col[3][2] = 0;
        col[3][3] = 0;
    }
}

static void blit_block(unsigned char* rgba, int w, int h, int bx, int by, const unsigned char col[4][4],
                       unsigned int bits, const unsigned char* alpha) {
    int py, px;
    for (py = 0; py < 4; py++) {
        for (px = 0; px < 4; px++) {
            int code = (int)((bits >> (2 * (py * 4 + px))) & 3);
            int x = bx + px;
            int y = by + py;
            unsigned char a = col[code][3];
            unsigned char* dst;
            if (alpha) a = alpha[py * 4 + px];
            if (x >= w || y >= h) continue;
            dst = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            dst[0] = col[code][0];
            dst[1] = col[code][1];
            dst[2] = col[code][2];
            dst[3] = a;
        }
    }
}

static void dxt3_alpha(const unsigned char* b, unsigned char alpha[16]) {
    int i;
    for (i = 0; i < 8; i++) {
        unsigned char byte = b[i];
        alpha[i * 2] = (unsigned char)((byte & 0x0f) * 17);
        alpha[i * 2 + 1] = (unsigned char)(((byte >> 4) & 0x0f) * 17);
    }
}

static void dxt5_alpha(const unsigned char* b, unsigned char alpha[16]) {
    unsigned char a0 = b[0], a1 = b[1];
    unsigned char pal[8];
    unsigned long long bits = 0;
    int i;
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        for (i = 1; i <= 6; i++) pal[i + 1] = (unsigned char)(((6 - (i - 1)) * a0 + i * a1) / 7);
    } else {
        for (i = 1; i <= 4; i++) pal[i + 1] = (unsigned char)(((4 - (i - 1)) * a0 + i * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    for (i = 0; i < 6; i++) bits |= ((unsigned long long)b[2 + i]) << (8 * i);
    for (i = 0; i < 16; i++) alpha[i] = pal[(bits >> (3 * i)) & 7];
}

static int read_dds(const unsigned char* p, size_t size, unsigned char** out, int* ow, int* oh) {
    unsigned int flags, fourcc, bitcount, hdr_flags;
    int width, height, four, block_bytes, bw, bh, by, bx;
    size_t off;
    unsigned char* rgba;
    if (size < 128 || memcmp(p, "DDS ", 4) != 0) return 0;
    hdr_flags = (unsigned int)p[8] | ((unsigned int)p[9] << 8) | ((unsigned int)p[10] << 16) | ((unsigned int)p[11] << 24);
    height = (int)(p[12] | (p[13] << 8) | (p[14] << 16) | (p[15] << 24));
    width = (int)(p[16] | (p[17] << 8) | (p[18] << 16) | (p[19] << 24));
    flags = (unsigned int)p[80] | ((unsigned int)p[81] << 8) | ((unsigned int)p[82] << 16) | ((unsigned int)p[83] << 24);
    fourcc = (unsigned int)p[84] | ((unsigned int)p[85] << 8) | ((unsigned int)p[86] << 16) | ((unsigned int)p[87] << 24);
    bitcount = (unsigned int)p[88] | ((unsigned int)p[89] << 8) | ((unsigned int)p[90] << 16) | ((unsigned int)p[91] << 24);
    (void)hdr_flags;
    if (!dims_ok(width, height)) return 0;
    rgba = alloc_rgba(width, height);
    if (!rgba) return 0;
    off = 128;
    if ((flags & 0x4) != 0) {
        if (fourcc == 0x31545844u) { /* DXT1 */
            four = 0;
            block_bytes = 8;
        } else if (fourcc == 0x33545844u) { /* DXT3 */
            four = 2;
            block_bytes = 16;
        } else if (fourcc == 0x35545844u) { /* DXT5 */
            four = 1;
            block_bytes = 16;
        } else {
            free(rgba);
            return 0;
        }
        bw = (width + 3) / 4;
        bh = (height + 3) / 4;
        if (off + (size_t)bw * (size_t)bh * (size_t)block_bytes > size) {
            free(rgba);
            return 0;
        }
        for (by = 0; by < bh; by++) {
            for (bx = 0; bx < bw; bx++) {
                const unsigned char* block = p + off;
                unsigned char col[4][4];
                unsigned char alpha[16];
                unsigned short c0, c1;
                unsigned int bits;
                const unsigned char* color = four ? block + 8 : block;
                off += (size_t)block_bytes;
                if (four == 1) dxt5_alpha(block, alpha);
                else if (four == 2) dxt3_alpha(block, alpha);
                c0 = ru16(color);
                c1 = ru16(color + 2);
                bits = (unsigned int)color[4] | ((unsigned int)color[5] << 8) |
                       ((unsigned int)color[6] << 16) | ((unsigned int)color[7] << 24);
                dxt_colors(c0, c1, four, col);
                blit_block(rgba, width, height, bx * 4, by * 4, col, bits, four ? alpha : NULL);
            }
        }
    } else if (bitcount == 24 || bitcount == 32) {
        int bytes = (int)bitcount / 8;
        unsigned int pitch = (unsigned int)(p[20] | (p[21] << 8) | (p[22] << 16) | (p[23] << 24));
        int y;
        if ((hdr_flags & 0x8) == 0 || pitch < (unsigned int)(width * bytes)) pitch = (unsigned int)(width * bytes);
        if (off + (size_t)pitch * (size_t)height > size) {
            free(rgba);
            return 0;
        }
        for (y = 0; y < height; y++) {
            int x;
            const unsigned char* row = p + off + (size_t)y * pitch;
            for (x = 0; x < width; x++) {
                const unsigned char* px = row + x * bytes;
                unsigned char a = bytes == 4 ? px[3] : 255;
                unsigned char* dst = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u;
                dst[0] = px[2];
                dst[1] = px[1];
                dst[2] = px[0];
                dst[3] = a;
            }
        }
    } else {
        free(rgba);
        return 0;
    }
    *out = rgba;
    *ow = width;
    *oh = height;
    return 1;
}

int texture_read_rgba(const void* data, size_t size, unsigned char** out_rgba, int* out_w, int* out_h) {
    const unsigned char* p = (const unsigned char*)data;
    if (!data || !out_rgba || !out_w || !out_h || size < 18) return 0;
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;
    if (size >= 4 && memcmp(p, "DDS ", 4) == 0) return read_dds(p, size, out_rgba, out_w, out_h);
    if (size >= 8 && memcmp(p, "\x89PNG", 4) == 0) return 0;
    return read_tga(p, size, out_rgba, out_w, out_h);
}
