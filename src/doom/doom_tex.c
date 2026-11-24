#include "doom_tex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    short width;
    short height;
    short leftoffset;
    short topoffset;
} DoomPatchHeader;

typedef struct {
    char name[8];
    int masked;
    short width;
    short height;
    int columndirectory;
    short patchcount;
} DoomTextureDef;

typedef struct {
    short originx;
    short originy;
    short patch;
    short stepdir;
    short colormap;
} DoomTexturePatch;
#pragma pack(pop)

static int tex_strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void sanitize_doom_name(const char* raw, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; i < 8 && raw[i] && j + 1 < cap; ++i) {
        char c = (char)tolower((unsigned char)raw[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

static const unsigned char* get_playpal(const DoomWad* w) {
    if (!w) return NULL;
    int idx = doom_wad_find_lump(w, "PLAYPAL", 0);
    if (idx < 0) return NULL;
    unsigned int sz = 0;
    const unsigned char* p = doom_wad_lump_ptr(w, idx, &sz);
    if (!p || sz < 768) return NULL;
    return p;
}

static int make_tga(const unsigned char* bgra, int width, int height,
                    unsigned char** out_tga, unsigned int* out_size) {
    if (!bgra || width <= 0 || height <= 0 || !out_tga || !out_size) return 0;
    unsigned int data_sz = (unsigned int)(width * height * 4);
    unsigned int total_sz = 18 + data_sz;
    unsigned char* buf = (unsigned char*)malloc(total_sz);
    if (!buf) return 0;

    memset(buf, 0, 18);
    buf[2] = 2; /* Uncompressed truecolor */
    buf[12] = (unsigned char)(width & 0xFF);
    buf[13] = (unsigned char)((width >> 8) & 0xFF);
    buf[14] = (unsigned char)(height & 0xFF);
    buf[15] = (unsigned char)((height >> 8) & 0xFF);
    buf[16] = 32; /* 32 bits per pixel */
    buf[17] = 0x28; /* Top-down (0x20) + 8-bit alpha (0x08) */

    memcpy(buf + 18, bgra, data_sz);
    *out_tga = buf;
    *out_size = total_sz;
    return 1;
}

/* Decode flat (64x64 bytes) */
static int decode_flat(const DoomWad* w, int lump_idx, const unsigned char* pal,
                       unsigned char** out_tga, unsigned int* out_size) {
    unsigned int sz = 0;
    const unsigned char* raw = doom_wad_lump_ptr(w, lump_idx, &sz);
    if (!raw || sz < 4096) return 0;

    unsigned char bgra[64 * 64 * 4];
    for (int i = 0; i < 4096; ++i) {
        unsigned char col = raw[i];
        bgra[i * 4 + 0] = pal[col * 3 + 2]; /* B */
        bgra[i * 4 + 1] = pal[col * 3 + 1]; /* G */
        bgra[i * 4 + 2] = pal[col * 3 + 0]; /* R */
        bgra[i * 4 + 3] = 255;              /* A */
    }
    return make_tga(bgra, 64, 64, out_tga, out_size);
}

/* Draw a Doom patch into a BGRA canvas */
static void draw_patch(const unsigned char* patch_data, unsigned int patch_sz,
                       int dest_ox, int dest_oy,
                       unsigned char* dest_bgra, int dest_w, int dest_h,
                       const unsigned char* pal) {
    if (!patch_data || patch_sz < sizeof(DoomPatchHeader)) return;
    const DoomPatchHeader* ph = (const DoomPatchHeader*)patch_data;
    int pw = ph->width;
    int ph_h = ph->height;
    if (pw <= 0 || ph_h <= 0 || pw > 2048 || ph_h > 2048) return;

    if (sizeof(DoomPatchHeader) + (size_t)pw * 4 > patch_sz) return;
    const unsigned int* col_ofs = (const unsigned int*)(patch_data + sizeof(DoomPatchHeader));

    for (int col = 0; col < pw; ++col) {
        int dx = dest_ox + col;
        if (dx < 0 || dx >= dest_w) continue;

        unsigned int ofs = col_ofs[col];
        if (ofs >= patch_sz) continue;

        const unsigned char* ptr = patch_data + ofs;
        const unsigned char* end = patch_data + patch_sz;

        while (ptr < end) {
            unsigned char topdelta = *ptr++;
            if (topdelta == 0xFF) break;
            if (ptr >= end) break;
            unsigned char length = *ptr++;
            if (ptr >= end) break;
            ptr++; /* Unused dummy byte */

            for (int p = 0; p < length && ptr < end; ++p) {
                int dy = dest_oy + topdelta + p;
                unsigned char col_idx = *ptr++;
                if (dy >= 0 && dy < dest_h) {
                    size_t pidx = ((size_t)dy * (size_t)dest_w + (size_t)dx) * 4;
                    dest_bgra[pidx + 0] = pal[col_idx * 3 + 2];
                    dest_bgra[pidx + 1] = pal[col_idx * 3 + 1];
                    dest_bgra[pidx + 2] = pal[col_idx * 3 + 0];
                    dest_bgra[pidx + 3] = 255;
                }
            }
            if (ptr < end) ptr++; /* Unused dummy byte */
        }
    }
}

/* Decode a composite wall texture from TEXTURE1 / TEXTURE2 */
static int decode_composite_wall(const DoomWad* w, const char* tex_name,
                                 const unsigned char* pal,
                                 unsigned char** out_tga, unsigned int* out_size) {
    int pnames_idx = doom_wad_find_lump(w, "PNAMES", 0);
    if (pnames_idx < 0) return 0;
    unsigned int pnames_sz = 0;
    const unsigned char* pnames_raw = doom_wad_lump_ptr(w, pnames_idx, &pnames_sz);
    if (!pnames_raw || pnames_sz < 4) return 0;

    int num_pnames = 0;
    memcpy(&num_pnames, pnames_raw, 4);
    if (num_pnames <= 0 || (size_t)num_pnames * 8 + 4 > pnames_sz) return 0;
    const char (*pnames)[8] = (const char (*)[8])(pnames_raw + 4);

    const char* tex_lump_names[2] = {"TEXTURE1", "TEXTURE2"};
    for (int tli = 0; tli < 2; ++tli) {
        int tl_idx = doom_wad_find_lump(w, tex_lump_names[tli], 0);
        if (tl_idx < 0) continue;
        unsigned int tl_sz = 0;
        const unsigned char* tl_raw = doom_wad_lump_ptr(w, tl_idx, &tl_sz);
        if (!tl_raw || tl_sz < 4) continue;

        int num_textures = 0;
        memcpy(&num_textures, tl_raw, 4);
        if (num_textures <= 0 || (size_t)num_textures * 4 + 4 > tl_sz) continue;
        const int* offsets = (const int*)(tl_raw + 4);

        for (int ti = 0; ti < num_textures; ++ti) {
            int ofs = offsets[ti];
            if (ofs <= 0 || (size_t)ofs + sizeof(DoomTextureDef) > tl_sz) continue;
            const DoomTextureDef* td = (const DoomTextureDef*)(tl_raw + ofs);

            char clean[32];
            sanitize_doom_name(td->name, clean, sizeof(clean));
            if (strcmp(clean, tex_name) != 0) continue;

            int tw = td->width;
            int th = td->height;
            if (tw <= 0 || th <= 0 || tw > 2048 || th > 2048) return 0;

            size_t bgra_sz = (size_t)tw * (size_t)th * 4;
            unsigned char* bgra = (unsigned char*)calloc(1, bgra_sz);
            if (!bgra) return 0;

            const DoomTexturePatch* patches = (const DoomTexturePatch*)(tl_raw + ofs + sizeof(DoomTextureDef));
            for (int pi = 0; pi < td->patchcount; ++pi) {
                if ((size_t)ofs + sizeof(DoomTextureDef) + (size_t)(pi + 1) * sizeof(DoomTexturePatch) > tl_sz) break;
                const DoomTexturePatch* tp = &patches[pi];
                int pidx = tp->patch;
                if (pidx < 0 || pidx >= num_pnames) continue;

                char pname[9];
                memcpy(pname, pnames[pidx], 8);
                pname[8] = '\0';
                int plump = doom_wad_find_lump(w, pname, 0);
                if (plump < 0) continue;

                unsigned int psz = 0;
                const unsigned char* pdata = doom_wad_lump_ptr(w, plump, &psz);
                if (!pdata) continue;

                draw_patch(pdata, psz, tp->originx, tp->originy, bgra, tw, th, pal);
            }

            int ok = make_tga(bgra, tw, th, out_tga, out_size);
            free(bgra);
            return ok;
        }
    }
    return 0;
}

int doom_tex_exists(const DoomWad* doom1, const DoomWad* doom2,
                    const char* tex_name) {
    if (!tex_name) return 0;
    const DoomWad* wads[2] = {doom1, doom2};
    for (int wi = 0; wi < 2; ++wi) {
        const DoomWad* w = wads[wi];
        if (!w) continue;

        /* Check TEXTURE1 / TEXTURE2 */
        const char* tex_lump_names[2] = {"TEXTURE1", "TEXTURE2"};
        for (int tli = 0; tli < 2; ++tli) {
            int tl_idx = doom_wad_find_lump(w, tex_lump_names[tli], 0);
            if (tl_idx < 0) continue;
            unsigned int tl_sz = 0;
            const unsigned char* tl_raw = doom_wad_lump_ptr(w, tl_idx, &tl_sz);
            if (!tl_raw || tl_sz < 4) continue;
            int num_textures = 0;
            memcpy(&num_textures, tl_raw, 4);
            if (num_textures <= 0 || (size_t)num_textures * 4 + 4 > tl_sz) continue;
            const int* offsets = (const int*)(tl_raw + 4);
            for (int ti = 0; ti < num_textures; ++ti) {
                int ofs = offsets[ti];
                if (ofs <= 0 || (size_t)ofs + sizeof(DoomTextureDef) > tl_sz) continue;
                const DoomTextureDef* td = (const DoomTextureDef*)(tl_raw + ofs);
                char clean[32];
                sanitize_doom_name(td->name, clean, sizeof(clean));
                if (strcmp(clean, tex_name) == 0) return 1;
            }
        }

        /* Check flat */
        int f_start = doom_wad_find_lump(w, "F_START", 0);
        if (f_start < 0) f_start = doom_wad_find_lump(w, "FF_START", 0);
        int f_end = doom_wad_find_lump(w, "F_END", f_start > 0 ? f_start : 0);
        if (f_end < 0) f_end = doom_wad_find_lump(w, "FF_END", 0);

        if (f_start >= 0 && f_end > f_start) {
            for (int li = f_start + 1; li < f_end; ++li) {
                char clean[32];
                sanitize_doom_name(w->lumps[li].name, clean, sizeof(clean));
                if (strcmp(clean, tex_name) == 0) return 1;
            }
        }

        int lidx = doom_wad_find_lump(w, tex_name, 0);
        if (lidx >= 0 && (w->lumps[lidx].size == 4096 || w->lumps[lidx].size > sizeof(DoomPatchHeader))) {
            return 1;
        }
    }
    return 0;
}

int doom_tex_extract(const DoomWad* doom1, const DoomWad* doom2,
                     const char* tex_name,
                     unsigned char** out_tga, unsigned int* out_size) {
    if (!tex_name || !out_tga || !out_size) return 0;

    const DoomWad* wads[2] = {doom1, doom2};
    for (int wi = 0; wi < 2; ++wi) {
        const DoomWad* w = wads[wi];
        if (!w) continue;

        const unsigned char* pal = get_playpal(w);
        if (!pal && wi == 1 && wads[0]) pal = get_playpal(wads[0]);
        if (!pal) continue;

        /* 1. Try composite wall texture */
        if (decode_composite_wall(w, tex_name, pal, out_tga, out_size)) return 1;

        /* 2. Try flat */
        int f_start = doom_wad_find_lump(w, "F_START", 0);
        if (f_start < 0) f_start = doom_wad_find_lump(w, "FF_START", 0);
        int f_end = doom_wad_find_lump(w, "F_END", f_start > 0 ? f_start : 0);
        if (f_end < 0) f_end = doom_wad_find_lump(w, "FF_END", 0);

        if (f_start >= 0 && f_end > f_start) {
            for (int li = f_start + 1; li < f_end; ++li) {
                char clean[32];
                sanitize_doom_name(w->lumps[li].name, clean, sizeof(clean));
                if (strcmp(clean, tex_name) == 0) {
                    if (decode_flat(w, li, pal, out_tga, out_size)) return 1;
                }
            }
        }

        /* 3. Fallback: try any lump by name matching 4096 bytes (flat) */
        int lidx = doom_wad_find_lump(w, tex_name, 0);
        if (lidx >= 0 && w->lumps[lidx].size == 4096) {
            if (decode_flat(w, lidx, pal, out_tga, out_size)) return 1;
        }

        /* 4. Fallback: try as raw patch lump */
        if (lidx >= 0 && w->lumps[lidx].size > sizeof(DoomPatchHeader)) {
            unsigned int psz = 0;
            const unsigned char* pdata = doom_wad_lump_ptr(w, lidx, &psz);
            if (pdata) {
                const DoomPatchHeader* ph = (const DoomPatchHeader*)pdata;
                int pw = ph->width, ph_h = ph->height;
                if (pw > 0 && ph_h > 0 && pw <= 2048 && ph_h <= 2048) {
                    unsigned char* bgra = (unsigned char*)calloc(1, (size_t)pw * (size_t)ph_h * 4);
                    if (bgra) {
                        draw_patch(pdata, psz, 0, 0, bgra, pw, ph_h, pal);
                        int ok = make_tga(bgra, pw, ph_h, out_tga, out_size);
                        free(bgra);
                        if (ok) return 1;
                    }
                }
            }
        }
    }
    return 0;
}
