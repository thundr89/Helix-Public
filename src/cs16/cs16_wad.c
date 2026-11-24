#include "cs16_wad.h"

#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    char name[16];
    unsigned int width;
    unsigned int height;
    unsigned int offsets[4];
} Wad3MipTex;
#pragma pack(pop)

int cs16_wad3_miptex_to_tga(const unsigned char* lump, size_t lump_size,
                            const char* tex_name,
                            unsigned char** out_buf, unsigned int* out_size) {
    if (!lump || lump_size < sizeof(Wad3MipTex) || !out_buf || !out_size) return 0;

    const Wad3MipTex* mt = (const Wad3MipTex*)lump;
    unsigned int w = mt->width;
    unsigned int h = mt->height;
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return 0;

    unsigned int mip0_ofs = mt->offsets[0];
    unsigned int mip3_ofs = mt->offsets[3];
    unsigned int w3 = w >> 3; if (w3 == 0) w3 = 1;
    unsigned int h3 = h >> 3; if (h3 == 0) h3 = 1;
    unsigned int mip0_size = w * h;
    unsigned int mip3_size = w3 * h3;

    if (mip0_ofs < sizeof(Wad3MipTex) || (size_t)mip0_ofs + mip0_size > lump_size) return 0;
    if (mip3_ofs < sizeof(Wad3MipTex) || (size_t)mip3_ofs + mip3_size + 2 + 768 > lump_size) return 0;

    const unsigned char* pixels = lump + mip0_ofs;
    const unsigned char* pal = lump + mip3_ofs + mip3_size + 2;

    int is_blue_key = (pal[255 * 3 + 0] == 0 && pal[255 * 3 + 1] == 0 && pal[255 * 3 + 2] == 255);
    int has_alpha = ((tex_name && tex_name[0] == '{') || is_blue_key) ? 1 : 0;

    /* 18-byte TGA header + w * h * 4 BGRA */
    size_t tga_size = 18 + (size_t)w * (size_t)h * 4;
    unsigned char* tga = (unsigned char*)malloc(tga_size);
    if (!tga) return 0;

    memset(tga, 0, 18);
    tga[2] = 2; /* Uncompressed true-color */
    tga[12] = (unsigned char)(w & 0xFF);
    tga[13] = (unsigned char)((w >> 8) & 0xFF);
    tga[14] = (unsigned char)(h & 0xFF);
    tga[15] = (unsigned char)((h >> 8) & 0xFF);
    tga[16] = 32; /* 32-bit BGRA */
    tga[17] = 0x20 | 0x08; /* Top-down origin + 8 alpha bits */

    unsigned char* dst = tga + 18;
    for (unsigned int i = 0; i < mip0_size; ++i) {
        unsigned char idx = pixels[i];
        unsigned char r = pal[idx * 3 + 0];
        unsigned char g = pal[idx * 3 + 1];
        unsigned char b = pal[idx * 3 + 2];
        int transparent = has_alpha && (idx == 255 || (r == 0 && g == 0 && b == 255));
        if (transparent) {
            dst[i * 4 + 0] = 0;
            dst[i * 4 + 1] = 0;
            dst[i * 4 + 2] = 0;
            dst[i * 4 + 3] = 0;
        } else {
            dst[i * 4 + 0] = b;
            dst[i * 4 + 1] = g;
            dst[i * 4 + 2] = r;
            dst[i * 4 + 3] = 255;
        }
    }

    /* Dilate opaque neighbor RGB into transparent pixels to prevent mipmap/bilinear bleeding */
    if (has_alpha) {
        for (unsigned int y = 0; y < h; ++y) {
            for (unsigned int x = 0; x < w; ++x) {
                unsigned int i = y * w + x;
                if (dst[i * 4 + 3] != 0) continue;
                unsigned int sr = 0, sg = 0, sb = 0, cnt = 0;
                const int dx[4] = { -1, 1, 0, 0 };
                const int dy[4] = { 0, 0, -1, 1 };
                for (int d = 0; d < 4; ++d) {
                    int nx = (int)x + dx[d];
                    int ny = (int)y + dy[d];
                    if (nx >= 0 && nx < (int)w && ny >= 0 && ny < (int)h) {
                        unsigned int ni = (unsigned int)ny * w + (unsigned int)nx;
                        if (dst[ni * 4 + 3] == 255) {
                            sb += dst[ni * 4 + 0];
                            sg += dst[ni * 4 + 1];
                            sr += dst[ni * 4 + 2];
                            cnt++;
                        }
                    }
                }
                if (cnt > 0) {
                    dst[i * 4 + 0] = (unsigned char)(sb / cnt);
                    dst[i * 4 + 1] = (unsigned char)(sg / cnt);
                    dst[i * 4 + 2] = (unsigned char)(sr / cnt);
                }
            }
        }
    }

    *out_buf = tga;
    *out_size = (unsigned int)tga_size;
    return 1;
}
