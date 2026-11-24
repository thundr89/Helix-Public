/*
 * Call of Duty 1 (2003) IBSP v59 Map Loader & .hxmap Converter
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Lives in the modular hxfs_cod plugin (src/cod). src/cod2003 is a separate plugin and is left unchanged.
 */

#include "bsp/helix.h"
#include "bsp/read.h"
#include "shader/helix.h"
#include "bot/cod_nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

#define COD_BSP_LUMPS 33

#define LUMP_MATERIALS      0
#define LUMP_LIGHTMAPS      1
#define LUMP_PLANES         2
#define LUMP_BRUSHSIDES     3
#define LUMP_BRUSHES        4
#define LUMP_LEAFSURFACES   5
#define LUMP_TRIANGLESOUPS  6
#define LUMP_VERTICES       7
#define LUMP_MESHVERTS      8
#define LUMP_TERRAIN_PATCHES   24
#define LUMP_TERRAIN_VERTICES  25
#define LUMP_TERRAIN_INDICES   26
#define LUMP_MODELS        27
#define LUMP_ENTITIES      29
#define TERRAIN_GRID       129

typedef struct {
    float normal[3];
    float dist;
} CoD1Plane;

typedef struct {
    union {
        float        dist;
        unsigned int plane;
    } column1;
    unsigned int material_id;
} CoD1BrushSide;

typedef struct {
    unsigned short sides;
    unsigned short material_id;
} CoD1Brush;

typedef struct {
    float mins[3];
    float maxs[3];
    int first_surf;
    int num_surfs;
    int unk1;
    int unk2;
    int first_brush;
    int num_brushes;
} CoD1Model;

typedef struct {
    unsigned int length;
    unsigned int offset;
} CoDLump;

typedef struct {
    char name[64];
    unsigned int flags;
    unsigned int content_flags;
} CoDMaterial;

typedef struct {
    unsigned short material_id;
    short lightmap_index;
    int            vertex_offset;
    unsigned short vertex_count;
    unsigned short triangle_count; // Number of indices in MeshVerts
    int            triangle_offset; // Index into MeshVerts
} CoDTriangleSoup;

typedef struct {
    float pos[3];
    float uv[2];
    float lightmap[2];
    float normal[3];
    float color;
} CoD1Vertex;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct {
    float min[3];
    float max[3];
} BrushBox;

static int name_has(const char* name, const char* bit) {
    return name && bit && strstr(name, bit) != NULL;
}

static int window_mat(const char* name) {
    return name_has(name, "glass") || name_has(name, "window") || name_has(name, "brokenglass");
}

static int debris_mat(const char* name) {
    return name_has(name, "rubble") || name_has(name, "debris") || name_has(name, "broken") ||
           name_has(name, "chunk") || name_has(name, "destroy") || name_has(name, "ruin");
}

static int boxes_overlap(const BrushBox* b, const float min[3], const float max[3]) {
    return min[0] <= b->max[0] && max[0] >= b->min[0] && min[1] <= b->max[1] && max[1] >= b->min[1] &&
           min[2] <= b->max[2] && max[2] >= b->min[2];
}

static void sb_init(StringBuilder* sb) {
    sb->cap = 1024 * 1024; // 1 MB initial capacity
    sb->data = (char*)malloc(sb->cap);
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

static void sb_append(StringBuilder* sb, const char* str) {
    size_t slen;
    if (!sb || !str) return;
    slen = strlen(str);
    if (sb->len + slen + 1 > sb->cap) {
        size_t ncap = sb->cap * 2;
        char* ndata;
        while (sb->len + slen + 1 > ncap) ncap *= 2;
        ndata = (char*)realloc(sb->data, ncap);
        if (!ndata) return;
        sb->data = ndata;
        sb->cap = ncap;
    }
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void sb_append_f(StringBuilder* sb, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sb_append(sb, buf);
}

int cod_bsp_is_valid(const void* data, size_t size) {
    return bsp_read_valid(data, size);
}

static int parse_lumps(const unsigned char* data, size_t size, CoDLump* lumps) {
    return bsp_read_lumps(data, size, (BspLump*)lumps);
}

static const char* parse_token(const char* p, const char* end, char* out, size_t out_cap) {
    size_t len = 0;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end) return NULL;
    if (*p == '"') {
        p++;
        while (p < end && *p != '"' && len + 1 < out_cap) {
            out[len++] = *p++;
        }
        if (p < end && *p == '"') p++;
    } else {
        while (p < end && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && len + 1 < out_cap) {
            out[len++] = *p++;
        }
    }
    out[len] = '\0';
    return p;
}

static int span_ok(size_t size, unsigned int offset, unsigned int length) {
    if (length == 0 || offset > size) return 0;
    return length <= size - offset;
}

static int read_i16(const unsigned char* p) {
    unsigned int u = (unsigned int)p[0] | ((unsigned int)p[1] << 8);
    return (u & 0x8000u) ? (int)u - 65536 : (int)u;
}

static unsigned int read_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int read_i32(const unsigned char* p) {
    return (int)read_u32(p);
}

static float read_f32(const unsigned char* p) {
    unsigned int u = read_u32(p);
    float v;
    memcpy(&v, &u, 4);
    return v;
}

static const char* terrain_contents(unsigned int cflags) {
    if ((cflags & 0x40000000u) != 0) return NULL;
    if ((cflags & 0x1u) != 0) return "solid";
    if ((cflags & 0x10000u) != 0) return "playerclip";
    return NULL;
}

static float* grid_at(float* grid, int x, int y) {
    return grid + ((x * TERRAIN_GRID + y) * 3);
}

static int curve_needs_split(const float* p0, const float* p1, const float* p2, int max_error) {
    float b0 = (p0[0] + p2[0]) - (p1[0] + p1[0]);
    float b1 = (p0[1] + p2[1]) - (p1[1] + p1[1]);
    float b2 = (p0[2] + p2[2]) - (p1[2] + p1[2]);
    float len2 = b0 * b0 + (b1 * b1 + b2 * b2);
    if (len2 <= 0.f) return 0;
    return (float)max_error < sqrtf(len2) * 0.25f;
}

static void subdivide_columns(float* grid, int* width, int height, int max_error) {
    int x = 0;
    while (x < *width - 2) {
        int y;
        for (y = 0; y < height; y++) {
            if (curve_needs_split(grid_at(grid, x, y), grid_at(grid, x + 1, y),
                                  grid_at(grid, x + 2, y), max_error)) {
                break;
            }
        }
        if (y == height) {
            x += 2;
            continue;
        }
        if (*width > TERRAIN_GRID - 2) break;
        for (y = 0; y < height; y++) {
            float p0[3], p1[3], p2[3], mid[3];
            int source, axis;
            memcpy(p0, grid_at(grid, x, y), sizeof(p0));
            memcpy(p1, grid_at(grid, x + 1, y), sizeof(p1));
            memcpy(p2, grid_at(grid, x + 2, y), sizeof(p2));
            for (source = *width - 1; source > x + 1; source--) {
                memcpy(grid_at(grid, source + 2, y), grid_at(grid, source, y), sizeof(p0));
            }
            for (axis = 0; axis < 3; axis++) {
                float m01 = (p0[axis] + p1[axis]) * 0.5f;
                float m12 = (p1[axis] + p2[axis]) * 0.5f;
                grid_at(grid, x + 1, y)[axis] = m01;
                grid_at(grid, x + 3, y)[axis] = m12;
                mid[axis] = (m01 + m12) * 0.5f;
            }
            memcpy(grid_at(grid, x + 2, y), mid, sizeof(mid));
        }
        *width += 2;
    }
}

static int columns_match(const float* grid, int x, int height) {
    int y, axis;
    for (y = 0; y < height; y++) {
        const float* a = grid_at((float*)grid, x, y);
        const float* b = grid_at((float*)grid, x + 1, y);
        for (axis = 0; axis < 3; axis++) {
            float d = a[axis] - b[axis];
            if (d < -0.1f || d > 0.1f) return 0;
        }
    }
    return 1;
}

static void remove_flat_columns(float* grid, int* width, int height) {
    int x;
    for (x = 0; x < *width - 1; x++) {
        int y, source;
        if (!columns_match(grid, x, height)) continue;
        for (y = 0; y < height; y++) {
            for (source = x + 2; source < *width; source++) {
                memcpy(grid_at(grid, source - 1, y), grid_at(grid, source, y), 3 * sizeof(float));
            }
        }
        *width -= 1;
        x -= 1;
    }
}

static void transpose_grid(float* grid, float* scratch, int* width, int* height) {
    int x, y;
    int w = *width;
    int h = *height;
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            memcpy(grid_at(scratch, y, x), grid_at(grid, x, y), 3 * sizeof(float));
        }
    }
    memcpy(grid, scratch, (size_t)TERRAIN_GRID * TERRAIN_GRID * 3 * sizeof(float));
    *width = h;
    *height = w;
}

/* Downward authored faces are the walkable side in CoD. Helix thickens along -normal,
 * so the stored normal has to point up or the slab floats above the ground. */
static int append_terrain_tri(StringBuilder* sb, int* opened, unsigned int* base,
                              const char* contents, const float a[3], const float b_in[3], const float c_in[3], unsigned int surface_flags) {
    float b[3], c[3];
    float e1x, e1y, e1z, e2x, e2y, e2z, nx, ny, nz, len2, inv;
    memcpy(b, b_in, sizeof(b));
    memcpy(c, c_in, sizeof(c));
    e1x = b[0] - a[0]; e1y = b[1] - a[1]; e1z = b[2] - a[2];
    e2x = c[0] - a[0]; e2y = c[1] - a[1]; e2z = c[2] - a[2];
    nx = e1y * e2z - e1z * e2y;
    ny = e1z * e2x - e1x * e2z;
    nz = e1x * e2y - e1y * e2x;
    if (nz < 0.f) {
        float swap[3];
        memcpy(swap, b, sizeof(swap));
        memcpy(b, c, sizeof(b));
        memcpy(c, swap, sizeof(c));
        nx = -nx; ny = -ny; nz = -nz;
    }
    len2 = nx * nx + ny * ny + nz * nz;
    if (len2 < 1e-8f) return 0;
    inv = 1.f / sqrtf(len2);
    nx *= inv; ny *= inv; nz *= inv;
    if (!*opened) {
        sb_append(sb, "mesh {\n");
        sb_append(sb, "  material \"textures/common/clip\"\n");
        sb_append_f(sb, "  contents %s\n", contents);
        sb_append_f(sb,"  surfaceFlags %d\n",(int)surface_flags);
        *opened = 1;
        *base = 0;
    }
    sb_append_f(sb, "  v %.1f %.1f %.1f 0.0000 0.0000 %.2f %.2f %.2f\n",
                a[0], a[1], a[2], nx, ny, nz);
    sb_append_f(sb, "  v %.1f %.1f %.1f 0.0000 0.0000 %.2f %.2f %.2f\n",
                b[0], b[1], b[2], nx, ny, nz);
    sb_append_f(sb, "  v %.1f %.1f %.1f 0.0000 0.0000 %.2f %.2f %.2f\n",
                c[0], c[1], c[2], nx, ny, nz);
    sb_append_f(sb, "  f %u %u %u\n", *base, *base + 1u, *base + 2u);
    *base += 3u;
    return 1;
}

static int read_disk_vert(const unsigned char* verts, unsigned int vert_n, unsigned int index, float out[3]) {
    unsigned int off;
    if (index >= vert_n) return 0;
    off = index * 12u;
    out[0] = read_f32(verts + off);
    out[1] = read_f32(verts + off + 4);
    out[2] = read_f32(verts + off + 8);
    return 1;
}

static void emit_terrain_collision(StringBuilder* sb, const unsigned char* bytes, size_t size,
                                   const CoDLump* lumps, const CoDMaterial* materials,
                                   unsigned int num_materials) {
    const unsigned char* patches;
    const unsigned char* verts;
    const unsigned char* indices;
    unsigned int patch_n, vert_n, index_n, p;
    float* grid = NULL;
    float* scratch = NULL;
    if (!materials || num_materials == 0) return;
    if (!span_ok(size, lumps[LUMP_TERRAIN_PATCHES].offset, lumps[LUMP_TERRAIN_PATCHES].length) ||
        (lumps[LUMP_TERRAIN_PATCHES].length % 16u) != 0) {
        return;
    }
    if (!span_ok(size, lumps[LUMP_TERRAIN_VERTICES].offset, lumps[LUMP_TERRAIN_VERTICES].length) ||
        (lumps[LUMP_TERRAIN_VERTICES].length % 12u) != 0) {
        return;
    }
    patches = bytes + lumps[LUMP_TERRAIN_PATCHES].offset;
    verts = bytes + lumps[LUMP_TERRAIN_VERTICES].offset;
    patch_n = lumps[LUMP_TERRAIN_PATCHES].length / 16u;
    vert_n = lumps[LUMP_TERRAIN_VERTICES].length / 12u;
    indices = NULL;
    index_n = 0;
    if (span_ok(size, lumps[LUMP_TERRAIN_INDICES].offset, lumps[LUMP_TERRAIN_INDICES].length) &&
        (lumps[LUMP_TERRAIN_INDICES].length % 2u) == 0) {
        indices = bytes + lumps[LUMP_TERRAIN_INDICES].offset;
        index_n = lumps[LUMP_TERRAIN_INDICES].length / 2u;
    }

    for (p = 0; p < patch_n; p++) {
        const unsigned char* rec = patches + p * 16u;
        int shader = read_i16(rec);
        int mode = rec[2];
        const char* contents;
        int opened = 0;
        unsigned int base = 0;
        if (shader < 0 || (unsigned int)shader >= num_materials) continue;
        contents = terrain_contents(materials[shader].content_flags);
        if (!contents) continue;

        if (mode == 0) {
            int width = read_i16(rec + 4);
            int height = read_i16(rec + 6);
            int max_error = read_i32(rec + 8);
            unsigned int first = read_u32(rec + 12);
            int x, y;
            unsigned int need;
            if (width < 3 || height < 3 || (width & 1) == 0 || (height & 1) == 0) continue;
            if (width > TERRAIN_GRID || height > TERRAIN_GRID) continue;
            need = (unsigned int)width * (unsigned int)height;
            if (first > vert_n || need > vert_n - first) continue;
            if (!grid) {
                grid = (float*)malloc((size_t)TERRAIN_GRID * TERRAIN_GRID * 3 * sizeof(float));
                scratch = (float*)malloc((size_t)TERRAIN_GRID * TERRAIN_GRID * 3 * sizeof(float));
                if (!grid || !scratch) {
                    free(grid);
                    free(scratch);
                    grid = NULL;
                    scratch = NULL;
                    continue;
                }
            }
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    float pt[3];
                    if (!read_disk_vert(verts, vert_n, first + (unsigned int)(y * width + x), pt)) continue;
                    memcpy(grid_at(grid, x, y), pt, sizeof(pt));
                }
            }
            subdivide_columns(grid, &width, height, max_error);
            remove_flat_columns(grid, &width, height);
            transpose_grid(grid, scratch, &width, &height);
            subdivide_columns(grid, &width, height, max_error);
            remove_flat_columns(grid, &width, height);
            for (y = 0; y < height - 1; y++) {
                for (x = 0; x < width - 1; x++) {
                    float p00[3], p10[3], p11[3], p01[3];
                    memcpy(p00, grid_at(grid, x, y), sizeof(p00));
                    memcpy(p10, grid_at(grid, x + 1, y), sizeof(p10));
                    memcpy(p11, grid_at(grid, x + 1, y + 1), sizeof(p11));
                    memcpy(p01, grid_at(grid, x, y + 1), sizeof(p01));
                    append_terrain_tri(sb, &opened, &base, contents, p00, p10, p11, materials[shader].flags);
                    append_terrain_tri(sb, &opened, &base, contents, p00, p11, p01, materials[shader].flags);
                }
            }
        } else if (indices) {
            int num_verts = read_i16(rec + 4);
            int num_idx = read_i16(rec + 6);
            unsigned int first_vert = read_u32(rec + 8);
            unsigned int first_index = read_u32(rec + 12);
            int t;
            if (num_verts < 3 || num_idx < 3) continue;
            if (first_vert > vert_n || (unsigned int)num_verts > vert_n - first_vert) continue;
            if (first_index > index_n || (unsigned int)num_idx > index_n - first_index) continue;
            for (t = 0; t + 2 < num_idx; t += 3) {
                int i0 = read_i16(indices + (first_index + (unsigned int)t) * 2u);
                int i1 = read_i16(indices + (first_index + (unsigned int)t + 1u) * 2u);
                int i2 = read_i16(indices + (first_index + (unsigned int)t + 2u) * 2u);
                float a[3], b[3], c[3];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) continue;
                if (!read_disk_vert(verts, vert_n, first_vert + (unsigned int)i0, a) ||
                    !read_disk_vert(verts, vert_n, first_vert + (unsigned int)i1, b) ||
                    !read_disk_vert(verts, vert_n, first_vert + (unsigned int)i2, c)) {
                    continue;
                }
                append_terrain_tri(sb, &opened, &base, contents, a, b, c, materials[shader].flags);
            }
        }
        if (opened) sb_append(sb, "}\n\n");
    }
    free(grid);
    free(scratch);
}

static int model_owner(const CoD1Model* models,unsigned int count,unsigned int index,int surface) {
    unsigned int i;
    for(i=0;i<count;i++) {
        int first=surface?models[i].first_surf:models[i].first_brush;
        int n=surface?models[i].num_surfs:models[i].num_brushes;
        if(first>=0 && n>0 && index>=(unsigned int)first && index-(unsigned int)first<(unsigned int)n)return (int)i;
    }
    return 0;
}
static void model_tags(StringBuilder* sb,const CoD1Model* models,const unsigned char* visible,const unsigned char* linked,unsigned int count,unsigned int index,int surface) {
    int owner=model_owner(models,count,index,surface);
    int vis=owner==0 || visible[owner];
    int lnk=owner==0 || (linked ? linked[owner] : visible[owner]);
    sb_append_f(sb,"  model %d\n  visible %d\n  linked %d\n",owner,vis,lnk);
}

int bsp_to_helix(const void* data, size_t size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size) {
    const unsigned char* bytes = (const unsigned char*)data;
    CoDLump lumps[COD_BSP_LUMPS];
    StringBuilder sb;
    const char* mname = (map_name && map_name[0]) ? map_name : "cod_map";
    float min_world[3] = {  999999.f,  999999.f,  999999.f };
    float max_world[3] = { -999999.f, -999999.f, -999999.f };
    int spawn_count = 0;
    unsigned int model_count = 0;
    const CoD1Model* models = NULL;
    unsigned char* visible_models = NULL;
    unsigned char* linked_models = NULL;
    unsigned char* trigger_models = NULL;
    float* model_origins = NULL;
    CodNavBuilder nav_builder;

    if (!cod_bsp_is_valid(data, size)) return 0;
    if (!parse_lumps(bytes, size, lumps)) return 0;

    sb_init(&sb);
    if (!sb.data) return 0;
    cod_nav_init(&nav_builder);

    if (span_ok(size, lumps[LUMP_MODELS].offset, lumps[LUMP_MODELS].length)) {
        model_count = lumps[LUMP_MODELS].length / sizeof(CoD1Model);
        models = (const CoD1Model*)(bytes + lumps[LUMP_MODELS].offset);
    }
    if (model_count) {
        visible_models = (unsigned char*)calloc(model_count, 1);
        linked_models = (unsigned char*)calloc(model_count, 1);
        trigger_models = (unsigned char*)calloc(model_count, 1);
        model_origins = (float*)calloc(model_count * 3, sizeof(float));
        if (!visible_models || !linked_models || !trigger_models || !model_origins) {
            free(visible_models); free(linked_models); free(trigger_models); free(model_origins);
            cod_nav_free(&nav_builder);
            free(sb.data); return 0;
        }
        visible_models[0] = 1;
        linked_models[0] = 1;
    }

    sb_append(&sb, "map {\n");
    sb_append(&sb, "  version 1\n");
    sb_append_f(&sb, "  name \"%s\"\n", mname);
    sb_append_f(&sb, "  title \"%s\"\n", mname);
    sb_append(&sb, "}\n\n");

    /* 1. Parse Entities (Lump 29) */
    if (lumps[LUMP_ENTITIES].length > 0 &&
        lumps[LUMP_ENTITIES].offset + lumps[LUMP_ENTITIES].length <= size) {
        const char* p = (const char*)(bytes + lumps[LUMP_ENTITIES].offset);
        const char* end = p + lumps[LUMP_ENTITIES].length;

        while (p < end) {
            while (p < end && isspace((unsigned char)*p)) p++;
            if (p >= end) break;
            if (*p == '{') {
                const char* entity_start=p+1;const char* entity_end;
                char classname[128] = {0};
                char origin[128] = {0};
                char angle[32] = {0};
                char angles[64] = {0};
                char targetname[128] = {0};
                char target[128] = {0};
                char model[256] = {0};
                char modelscale[32] = {0};
                char key[128];
                char val[256];

                p++;
                while (p < end) {
                    while (p < end && isspace((unsigned char)*p)) p++;
                    if (p >= end || *p == '}') break;
                    p = parse_token(p, end, key, sizeof(key));
                    if (!p) break;
                    p = parse_token(p, end, val, sizeof(val));
                    if (!p) break;

                    if (strcmp(key, "classname") == 0) {
                        strncpy(classname, val, sizeof(classname)-1);
                    } else if (strcmp(key, "origin") == 0) {
                        strncpy(origin, val, sizeof(origin)-1);
                    } else if (strcmp(key, "angle") == 0) {
                        strncpy(angle, val, sizeof(angle)-1);
                    } else if (strcmp(key, "angles") == 0) {
                        strncpy(angles, val, sizeof(angles)-1);
                    } else if (strcmp(key, "targetname") == 0) {
                        strncpy(targetname, val, sizeof(targetname)-1);
                    } else if (strcmp(key, "target") == 0) {
                        strncpy(target, val, sizeof(target)-1);
                    } else if (strcmp(key, "model") == 0) {
                        strncpy(model, val, sizeof(model)-1);
                    } else if (strcmp(key, "modelscale") == 0) {
                        strncpy(modelscale, val, sizeof(modelscale)-1);
                    }
                }
                entity_end=p;
                if (p < end && *p == '}') p++;

                /* Retail exploder replacements are initially hidden. Keep the
                 * intact script brush models, including roofs and their clips. */
                if (model[0] == '*') {
                    char* tail;
                    long index = strtol(model + 1, &tail, 10);
                    if (*tail == '\0' && index > 0 && (unsigned long)index < model_count) {
                        float mox = 0.f, moy = 0.f, moz = 0.f;
                        if (origin[0] && sscanf(origin, "%f %f %f", &mox, &moy, &moz) == 3) {
                            model_origins[index * 3 + 0] = mox;
                            model_origins[index * 3 + 1] = moy;
                            model_origins[index * 3 + 2] = moz;
                        }
                        if (visible_models && strcmp(classname, "script_brushmodel") == 0 &&
                            strcmp(targetname, "exploder") != 0 &&
                            strcmp(targetname, "exploderchunk") != 0 &&
                            !origin[0] && !angles[0] && !angle[0]) {
                            visible_models[index] = 1;
                            linked_models[index] = 1;
                        } else if (linked_models && strncmp(classname, "trigger_", 8) == 0) {
                            linked_models[index] = 1;
                            trigger_models[index] = 1;
                        }
                    }
                }

                /* Map CoD classnames to Helix entity classes */
                if (classname[0]) {
                    const char* mapped_class = classname;
                    float ox = 0, oy = 0, oz = 0;
                    if (origin[0] && sscanf(origin, "%f %f %f", &ox, &oy, &oz) == 3) {
                        if (ox < min_world[0]) min_world[0] = ox;
                        if (oy < min_world[1]) min_world[1] = oy;
                        if (oz < min_world[2]) min_world[2] = oz;
                        if (ox > max_world[0]) max_world[0] = ox;
                        if (oy > max_world[1]) max_world[1] = oy;
                        if (oz > max_world[2]) max_world[2] = oz;
                    }

                    if (strcmp(classname, "mp_deathmatch_spawn") == 0 ||
                        strcmp(classname, "mp_teamdeathmatch_spawn") == 0 ||
                        strcmp(classname, "mp_allied_spawn") == 0 ||
                        strcmp(classname, "mp_axis_spawn") == 0 ||
                        strcmp(classname, "mp_searchanddestroy_spawn_allied") == 0 ||
                        strcmp(classname, "mp_searchanddestroy_spawn_axis") == 0 ||
                        strcmp(classname, "mp_retrieval_spawn_allied") == 0 ||
                        strcmp(classname, "mp_retrieval_spawn_axis") == 0 ||
                        strcmp(classname, "info_player_deathmatch") == 0) {
                        mapped_class = "info_player_deathmatch";
                        spawn_count++;
                    } else if (strcmp(classname, "info_player_start") == 0) {
                        mapped_class = "info_player_deathmatch";
                        spawn_count++;
                    }

                    if (origin[0] && (strcmp(mapped_class, "info_player_deathmatch") == 0 ||
                                      strcmp(mapped_class, "info_player_start") == 0 ||
                                      strcmp(classname, "trigger_use") == 0 ||
                                      strcmp(classname, "misc_mg42") == 0 ||
                                      strncmp(classname, "mpweapon_", 9) == 0 ||
                                      strstr(targetname, "bomb") != NULL ||
                                      strstr(targetname, "radio") != NULL ||
                                      strstr(targetname, "obj") != NULL)) {
                        { float nav_o[3] = { ox, oy, oz }; cod_nav_add_anchor_point(&nav_builder, nav_o, 0); }
                    }

                    /* Emit player spawn entity */
                    if (strcmp(mapped_class, "info_player_deathmatch") == 0 ||
                        strcmp(mapped_class, "info_player_start") == 0) {
                        sb_append(&sb, "entity {\n");
                        sb_append_f(&sb, "  \"classname\" \"%s\"\n", mapped_class);
                        sb_append_f(&sb, "  \"cod_classname\" \"%s\"\n", classname);
                        sb_append(&sb,"  \"spawn_anchor\" \"1\"\n");
                        if(origin[0])sb_append_f(&sb,"  \"cod_origin\" \"%s\"\n",origin);
                        if (origin[0]) {
                            float ox = 0.f, oy = 0.f, oz = 0.f;
                            if (sscanf(origin, "%f %f %f", &ox, &oy, &oz) == 3) {
                                sb_append_f(&sb, "  \"origin\" \"%.1f %.1f %.1f\"\n", ox, oy, oz + 32.f);
                            } else {
                                sb_append_f(&sb, "  \"origin\" \"%s\"\n", origin);
                            }
                        }
                        if (angle[0]) sb_append_f(&sb, "  \"angle\" \"%s\"\n", angle);
                        else if (angles[0]) sb_append_f(&sb, "  \"angles\" \"%s\"\n", angles);
                        if (targetname[0]) sb_append_f(&sb, "  \"targetname\" \"%s\"\n", targetname);
                        if (target[0]) sb_append_f(&sb, "  \"target\" \"%s\"\n", target);
                        sb_append(&sb, "}\n\n");
                    }

                    /* Preserve native spawn arguments for runtime getent/getentarray.
                     * Player spawn translation above remains the engine spawn source. */
                    if(strcmp(mapped_class,"info_player_deathmatch")!=0 && strcmp(mapped_class,"info_player_start")!=0) {
                        const char* q=entity_start;
                        int is_intermission = strstr(classname,"intermission")!=NULL;
                        int is_target = strcmp(classname,"info_null")==0 || strcmp(classname,"info_notnull")==0;
                        sb_append(&sb,"entity {\n");
                        sb_append_f(&sb,"  \"runtime_only\" \"%d\"\n", (is_intermission || is_target) ? 0 : 1);
                        if(is_intermission) {
                            sb_append(&sb,"  \"spawn_anchor\" \"1\"\n");
                            if(origin[0])sb_append_f(&sb,"  \"cod_origin\" \"%s\"\n",origin);
                        }
                        if((strcmp(classname,"script_model")==0 || strcmp(classname,"misc_mg42")==0) && model[0] && model[0]!='*' && strcmp(model,"xmodel/fx")!=0)
                            sb_append(&sb,"  \"render_model\" \"1\"\n");
                        while(q && q<entity_end) {
                            q=parse_token(q,entity_end,key,sizeof(key));if(!q || q>=entity_end)break;
                            q=parse_token(q,entity_end,val,sizeof(val));if(!q)break;
                            /* CoD stores sun RGB as a hue, including legacy 0..255 values. */
                            if(!strcmp(classname,"worldspawn") && (!strcmp(key,"suncolor") || !strcmp(key,"sundiffusecolor"))){
                                float r,g,b;if(sscanf(val,"%f %f %f",&r,&g,&b)==3){
                                    float peak=fmaxf(r,fmaxf(g,b));
                                    if(isfinite(peak) && peak>0.f){snprintf(val,sizeof(val),"%.9g %.9g %.9g",r/peak,g/peak,b/peak);}
                                }
                            }
                            sb_append_f(&sb,"  \"%s\" \"%s\"\n",key,val);
                            if(!strcmp(key,"lightingPrecalc")){
                                float r,g,b;if(sscanf(val,"%f %f %f",&r,&g,&b)==3){
                                    float peak;r*=2;g*=2;b*=2;peak=fmaxf(1.f,fmaxf(r,fmaxf(g,b)));
                                    if(r==0.f && g==0.f && b==0.f)
                                        sb_append(&sb,"  \"lightingColor\" \"-1 -1 -1\"\n"); /* Missing baked sample: use runtime lighting. */
                                    else
                                    sb_append_f(&sb,"  \"lightingColor\" \"%.9g %.9g %.9g\"\n",r/peak,g/peak,b/peak);
                                }
                            }
                        }
                        if(!strncmp(classname,"mpweapon_",9)){
                            const char* w=classname+9;
                            if(!strcmp(w,"kar98ksniper"))w="kar98k_sniper";
                            if(!strcmp(w,"mosinnagant"))w="mosin_nagant";
                            if(!strcmp(w,"mosinnagantsniper"))w="mosin_nagant_sniper";
                            sb_append_f(&sb,"  \"classname\" \"item_weapon_native\"\n  \"cod_classname\" \"%s\"\n  \"weapon\" \"%s_mp\"\n  \"runtime_only\" \"0\"\n",classname,w);
                            sb_append(&sb,"  \"drop_to_floor\" \"1\"\n");
                        }
                        if(model[0]=='*') {
                            int id=atoi(model+1);
                            int vis=id>0 && (unsigned int)id<model_count && visible_models[id];
                            int lnk=id>0 && (unsigned int)id<model_count && linked_models[id];
                            sb_append_f(&sb,"  \"visible\" \"%d\"\n  \"linked\" \"%d\"\n",vis,lnk);
                        } else if(strcmp(model,"xmodel/fx")==0 || strstr(targetname,"exploder")) {
                            sb_append(&sb,"  \"visible\" \"0\"\n");
                        }
                        sb_append(&sb,"}\n\n");
                    }
                }
            } else {
                p++;
            }
        }
    }

    /* Fallback spawn if none found */
    if (spawn_count == 0) {
        sb_append(&sb, "entity {\n");
        sb_append(&sb, "  \"classname\" \"info_player_deathmatch\"\n");
        sb_append(&sb, "  \"origin\" \"0 0 24\"\n");
        sb_append(&sb, "}\n\n");
        min_world[0] = -256.f; min_world[1] = -256.f; min_world[2] = 0.f;
        max_world[0] =  256.f; max_world[1] =  256.f; max_world[2] = 128.f;
        { float nav_o[3] = { 0.f, 0.f, 0.f }; cod_nav_add_anchor_point(&nav_builder, nav_o, 0); }
    }

    /* 2. Parse Brushes (Lump 4), BrushSides (Lump 3), Planes (Lump 2), Materials (Lump 0) */
    int brushes_emitted = 0;
    BrushBox* brush_boxes = NULL;
    int brush_box_n = 0;
    int brush_box_cap = 0;
    unsigned int num_materials = 0;
    const CoDMaterial* materials = NULL;
    if (lumps[LUMP_MATERIALS].length > 0) {
        num_materials = lumps[LUMP_MATERIALS].length / sizeof(CoDMaterial);
        materials = (const CoDMaterial*)(bytes + lumps[LUMP_MATERIALS].offset);
    }

    if(materials){unsigned int i;const char* suffix[]={"rt","lf","bk","ft","up","dn"};
        for(i=0;i<num_materials;i++){const char* base=shader_skybox(materials[i].name);int j;if(!base[0])continue;
            sb_append(&sb,"entity {\n  \"classname\" \"environment_skybox\"\n  \"runtime_only\" \"1\"\n");
            for(j=0;j<6;j++)sb_append_f(&sb,"  \"face_%s\" \"%s_%s\"\n",suffix[j],base,suffix[j]);
            if(shader_sunfile(materials[i].name)[0])sb_append_f(&sb,"  \"sun_profile\" \"%s\"\n",shader_sunfile(materials[i].name));
            sb_append(&sb,"}\n");break;
        }
    }

    if (lumps[LUMP_BRUSHES].length > 0 &&
        lumps[LUMP_BRUSHSIDES].length > 0) {

        unsigned int num_brushes = lumps[LUMP_BRUSHES].length / sizeof(CoD1Brush);
        const CoD1Brush* brushes = (const CoD1Brush*)(bytes + lumps[LUMP_BRUSHES].offset);

        unsigned int num_brushsides = lumps[LUMP_BRUSHSIDES].length / sizeof(CoD1BrushSide);
        const CoD1BrushSide* brushsides = (const CoD1BrushSide*)(bytes + lumps[LUMP_BRUSHSIDES].offset);

        unsigned int num_planes = (lumps[LUMP_PLANES].length > 0) ? (lumps[LUMP_PLANES].length / sizeof(CoD1Plane)) : 0;
        const CoD1Plane* planes = (num_planes > 0) ? (const CoD1Plane*)(bytes + lumps[LUMP_PLANES].offset) : NULL;

        unsigned int side_idx = 0;
        unsigned int b;

        for (b = 0; b < num_brushes; b++) {
            const CoD1Brush* brush = &brushes[b];
            unsigned int num_sides = brush->sides;

            if (side_idx + num_sides > num_brushsides) break;

            if (num_sides >= 6) {
                const char* mat_name = "textures/dev/wall";
                unsigned int cflags = 0;
                int owner = model_owner(models, model_count, b, 0);
                if (materials && brush->material_id < num_materials) {
                    if (materials[brush->material_id].name[0]) {
                        mat_name = materials[brush->material_id].name;
                    }
                    cflags = materials[brush->material_id].content_flags;
                }

                int is_weapclip = (strstr(mat_name, "weapclip") != NULL ||
                                   strstr(mat_name, "weaponclip") != NULL ||
                                   strstr(mat_name, "clip_weapon") != NULL ||
                                   strstr(mat_name, "clipweap") != NULL ||
                                   strstr(mat_name, "bulletclip") != NULL ||
                                   strstr(mat_name, "clipshot") != NULL);
                /* clipfoliage has no hard player collision. Straight hedges use clip. */
                int is_foliage_clip = strstr(mat_name, "clipfoliage") != NULL;
                int is_trigger = (strstr(mat_name, "trigger") != NULL || (cflags & 0x40000000) != 0 ||
                                  (owner > 0 && trigger_models && trigger_models[owner]));
                int is_playerclip = !is_weapclip && !is_foliage_clip && !is_trigger &&
                                    ((cflags & 0x10000u) != 0 ||
                                     strstr(mat_name, "playerclip") != NULL ||
                                     (strstr(mat_name, "clip") != NULL &&
                                      strstr(mat_name, "monster") == NULL &&
                                      strstr(mat_name, "shot") == NULL));
                int is_ladder = strstr(mat_name, "ladder") != NULL ||
                                (materials && brush->material_id < num_materials &&
                                 (materials[brush->material_id].flags & 0x8u) != 0);
                int is_shotclip = !is_playerclip && !is_ladder && !is_trigger && (cflags & 0x2080u) != 0;
                int is_tool = (is_weapclip ||
                               (materials && (cflags & 0x10001u) == 0 && !is_trigger && !is_shotclip &&
                                strstr(mat_name, "water") == NULL) ||
                               strstr(mat_name, "portal") != NULL ||
                               strstr(mat_name, "sky") != NULL ||
                               strstr(mat_name, "origin") != NULL ||
                               strstr(mat_name, "hint") != NULL ||
                               strstr(mat_name, "skip") != NULL ||
                               strstr(mat_name, "lightgrid") != NULL ||
                               strstr(mat_name, "nodraw_notsolid") != NULL ||
                               strstr(mat_name, "nosight_noclip") != NULL ||
                               strstr(mat_name, "notsolid") != NULL ||
                               strstr(mat_name, "descript") != NULL ||
                               strstr(mat_name, "aitrig") != NULL ||
                               strstr(mat_name, "spawner_trigger") != NULL ||
                               strstr(mat_name, "occluder") != NULL);

                /* Entity trigger hulls remain queryable even with skip/notsolid tools. */
                if(is_trigger)is_tool=0;
                if (!is_trigger && is_foliage_clip) {
                    /* Foliage volumes do not create hard player collision, including tops. */
                } else if (!is_trigger && window_mat(mat_name) && name_has(mat_name, "glass")) {
                    /* The pane fills the crouch opening. The wall brushes around it stay. */
                } else if (!is_tool) {
                    float d0 = brushsides[side_idx + 0].column1.dist;
                    float d1 = brushsides[side_idx + 1].column1.dist;
                    float d2 = brushsides[side_idx + 2].column1.dist;
                    float d3 = brushsides[side_idx + 3].column1.dist;
                    float d4 = brushsides[side_idx + 4].column1.dist;
                    float d5 = brushsides[side_idx + 5].column1.dist;
                    float ox = (owner > 0 && model_origins) ? model_origins[owner * 3 + 0] : 0.f;
                    float oy = (owner > 0 && model_origins) ? model_origins[owner * 3 + 1] : 0.f;
                    float oz = (owner > 0 && model_origins) ? model_origins[owner * 3 + 2] : 0.f;

                    float min_x = ((d0 < d1) ? d0 : d1) + ox;
                    float max_x = ((d0 < d1) ? d1 : d0) + ox;
                    float min_y = ((d2 < d3) ? d2 : d3) + oy;
                    float max_y = ((d2 < d3) ? d3 : d2) + oy;
                    float min_z = ((d4 < d5) ? d4 : d5) + oz;
                    float max_z = ((d4 < d5) ? d5 : d4) + oz;

                    /* Skip degenerate zero-volume brushes */
                    if ((max_x - min_x) >= 0.1f && (max_y - min_y) >= 0.1f && (max_z - min_z) >= 0.1f) {
                        sb_append(&sb, "brush {\n");
                        sb_append_f(&sb,"  surfaceFlags %d\n",materials && brush->material_id<num_materials?(int)materials[brush->material_id].flags:0);
                        model_tags(&sb,models,visible_models,linked_models,model_count,b,0);
                        sb_append_f(&sb, "  mins %.1f %.1f %.1f\n", min_x, min_y, min_z);
                        sb_append_f(&sb, "  maxs %.1f %.1f %.1f\n", max_x, max_y, max_z);

                        {
                            unsigned int s;
                            /* Every side plane. An AABB of the first six distances
                             * grows a slanted brush into the opening it was cut around. */
                            sb_append_f(&sb, "  plane -1.0 0.0 0.0 %.1f\n", -min_x);
                            sb_append_f(&sb, "  plane 1.0 0.0 0.0 %.1f\n", max_x);
                            sb_append_f(&sb, "  plane 0.0 -1.0 0.0 %.1f\n", -min_y);
                            sb_append_f(&sb, "  plane 0.0 1.0 0.0 %.1f\n", max_y);
                            sb_append_f(&sb, "  plane 0.0 0.0 -1.0 %.1f\n", -min_z);
                            sb_append_f(&sb, "  plane 0.0 0.0 1.0 %.1f\n", max_z);
                            if (planes) {
                                for (s = 6; s < num_sides; s++) {
                                    unsigned int p_idx = brushsides[side_idx + s].column1.plane;
                                    if (p_idx < num_planes) {
                                        const CoD1Plane* pl = &planes[p_idx];
                                        float pdist = pl->dist + pl->normal[0] * ox + pl->normal[1] * oy + pl->normal[2] * oz;
                                        sb_append_f(&sb, "  plane %.4f %.4f %.4f %.2f\n",
                                                    pl->normal[0], pl->normal[1], pl->normal[2], pdist);
                                    }
                                }
                            }
                        }

                        {unsigned int side,plane_index=0;
                         for(side=0;side<num_sides;side++){
                            unsigned int material=brushsides[side_idx+side].material_id;
                            if(side>=6 && (!planes || brushsides[side_idx+side].column1.plane>=num_planes))continue;
                            sb_append_f(&sb,"  planeSurfaceFlags %u %d\n",plane_index++,materials && material<num_materials?(int)materials[material].flags:0);
                         }}
                        sb_append(&sb, "  material \"textures/common/nodraw\"\n");
                        if (is_trigger) sb_append(&sb, "  contents trigger\n");
                        else if (is_ladder) sb_append(&sb, "  contents ladder\n");
                        else if (is_playerclip) sb_append(&sb, "  contents playerclip\n");
                        else if (is_shotclip) sb_append(&sb, "  contents shotclip\n");
                        else if (is_trigger) sb_append(&sb, "  contents trigger\n");
                        else if (strstr(mat_name, "water") != NULL) sb_append(&sb, "  contents water\n");
                        else sb_append(&sb, "  contents solid\n");
                        sb_append(&sb, "}\n\n");
                        brushes_emitted++;
                        if (is_ladder) {
                            { float nav_a[3] = { min_x, min_y, min_z }, nav_b[3] = { max_x, max_y, max_z }; cod_nav_add_ladder_box(&nav_builder, nav_a, nav_b); }
                        }
                        if (brush_box_n >= brush_box_cap) {
                            int ncap = brush_box_cap ? brush_box_cap * 2 : 256;
                            BrushBox* grown = (BrushBox*)realloc(brush_boxes, (size_t)ncap * sizeof(BrushBox));
                            if (grown) {
                                brush_boxes = grown;
                                brush_box_cap = ncap;
                            }
                        }
                        if ((owner==0 || visible_models[owner]) && brush_box_n < brush_box_cap && !is_shotclip && !is_ladder && !is_trigger &&
                            strstr(mat_name, "water") == NULL) {
                            brush_boxes[brush_box_n].min[0] = min_x;
                            brush_boxes[brush_box_n].min[1] = min_y;
                            brush_boxes[brush_box_n].min[2] = min_z;
                            brush_boxes[brush_box_n].max[0] = max_x;
                            brush_boxes[brush_box_n].max[1] = max_y;
                            brush_boxes[brush_box_n].max[2] = max_z;
                            brush_box_n++;
                            { float nav_a[3] = { min_x, min_y, min_z }, nav_b[3] = { max_x, max_y, max_z }; cod_nav_add_walkable_box(&nav_builder, nav_a, nav_b); }
                        }
                    }
                }
            }
            side_idx += num_sides;
        }
    }

    /* 3. Parse TriangleSoups (Lump 6), Vertices (Lump 7), MeshVerts (Lump 8) */
    if (lumps[LUMP_TRIANGLESOUPS].length > 0 &&
        lumps[LUMP_VERTICES].length > 0 &&
        lumps[LUMP_MESHVERTS].length > 0) {

        unsigned int num_soups = lumps[LUMP_TRIANGLESOUPS].length / sizeof(CoDTriangleSoup);
        const CoDTriangleSoup* soups = (const CoDTriangleSoup*)(bytes + lumps[LUMP_TRIANGLESOUPS].offset);

        unsigned int num_verts = lumps[LUMP_VERTICES].length / sizeof(CoD1Vertex);
        const CoD1Vertex* verts = (const CoD1Vertex*)(bytes + lumps[LUMP_VERTICES].offset);

        unsigned int num_indices = lumps[LUMP_MESHVERTS].length / sizeof(unsigned short);
        const unsigned short* indices = (const unsigned short*)(bytes + lumps[LUMP_MESHVERTS].offset);

        unsigned int s;
        int meshes_emitted = 0;

        for (s = 0; s < num_soups; s++) {

            const CoDTriangleSoup* soup = &soups[s];
            unsigned int v, t;

            if (soup->vertex_count < 3 || soup->triangle_count < 3) continue;
            if ((unsigned int)soup->vertex_offset + soup->vertex_count > num_verts) continue;
            if ((unsigned int)soup->triangle_offset + soup->triangle_count > num_indices) continue;

            const char* mat_name = "textures/dev/floor";
            if (materials && soup->material_id < num_materials && materials[soup->material_id].name[0]) {
                mat_name = materials[soup->material_id].name;
            }

            unsigned int cflags = (materials && soup->material_id < num_materials) ? materials[soup->material_id].content_flags : 0;
            int is_tool = (strstr(mat_name, "portal") != NULL ||
                           strstr(mat_name, "trigger") != NULL ||
                           strstr(mat_name, "clipmissile") != NULL ||
                           strstr(mat_name, "clip_nosight") != NULL ||
                           strstr(mat_name, "nodraw") != NULL ||
                           strstr(mat_name, "caulk") != NULL ||
                           strstr(mat_name, "hint") != NULL ||
                           strstr(mat_name, "skip") != NULL ||
                           strstr(mat_name, "lightgrid") != NULL ||
                           strstr(mat_name, "origin") != NULL ||
                           strstr(mat_name, "sky") != NULL ||
                           (strstr(mat_name, "decal") != NULL &&
                            (strstr(mat_name, "bullet") != NULL || strstr(mat_name, "blood") != NULL ||
                             strstr(mat_name, "impact") != NULL || strstr(mat_name, "burn") != NULL)) ||
                           strstr(mat_name, "shadow") != NULL ||
                           strstr(mat_name, "clipmonster") != NULL ||
                           strstr(mat_name, "clipfoliage") != NULL ||
                           strstr(mat_name, "nosight_noclip") != NULL ||
                           strstr(mat_name, "nodraw_notsolid") != NULL ||
                           strstr(mat_name, "notsolid") != NULL ||
                           strstr(mat_name, "ladder") != NULL ||
                           strstr(mat_name, "corona") != NULL ||
                           strstr(mat_name, "flare") != NULL ||
                           strstr(mat_name, "sprite") != NULL ||
                           strstr(mat_name, "sfx/beam") != NULL ||
                           strstr(mat_name, "weapclip") != NULL ||
                           strstr(mat_name, "weaponclip") != NULL ||
                           strstr(mat_name, "clip_weapon") != NULL ||
                           strstr(mat_name, "clipweap") != NULL ||
                           strstr(mat_name, "bulletclip") != NULL ||
                           strstr(mat_name, "clipshot") != NULL ||
                           strstr(mat_name, "clip") != NULL ||
                           (cflags & 0x40000000) != 0 /* TRIGGER */ ||
                           shader_is_invisible(mat_name));
            if (is_tool) continue;

            int is_foliage = (strstr(mat_name, "foliage") != NULL ||
                              strstr(mat_name, "bush") != NULL ||
                              strstr(mat_name, "plant") != NULL ||
                              strstr(mat_name, "shrub") != NULL ||
                              strstr(mat_name, "vine") != NULL ||
                              strstr(mat_name, "ivy") != NULL ||
                              strstr(mat_name, "hedge") != NULL ||
                              strstr(mat_name, "treeline") != NULL);

            const char* cont = "nonsolid";
            int is_window = window_mat(mat_name);
            int is_debris = !is_window && debris_mat(mat_name);
            int solid_bits = (cflags & 0x10001) != 0;
            int nonsolid_flag = (cflags & 0x20000000) && !solid_bits;
            int walkable = 0;
            int hits_brush = 0;
            float bmin[3], bmax[3];
            {
                int up = 0, tris = 0, bi;
                unsigned int vv;
                bmin[0] = bmin[1] = bmin[2] = 1.0e30f;
                bmax[0] = bmax[1] = bmax[2] = -1.0e30f;
                for (vv = 0; vv < soup->vertex_count; vv++) {
                    const CoD1Vertex* vert = &verts[soup->vertex_offset + vv];
                    int a;
                    for (a = 0; a < 3; a++) {
                        if (vert->pos[a] < bmin[a]) bmin[a] = vert->pos[a];
                        if (vert->pos[a] > bmax[a]) bmax[a] = vert->pos[a];
                    }
                }
                for (t = 0; t + 2 < soup->triangle_count; t += 3) {
                    unsigned short i0 = indices[soup->triangle_offset + t];
                    unsigned short i1 = indices[soup->triangle_offset + t + 1];
                    unsigned short i2 = indices[soup->triangle_offset + t + 2];
                    const CoD1Vertex* a;
                    const CoD1Vertex* b;
                    const CoD1Vertex* c;
                    float e1x, e1y, e1z, e2x, e2y, e2z, nz;
                    if (i0 >= soup->vertex_count || i1 >= soup->vertex_count || i2 >= soup->vertex_count) continue;
                    a = &verts[soup->vertex_offset + i0];
                    b = &verts[soup->vertex_offset + i1];
                    c = &verts[soup->vertex_offset + i2];
                    e1x = b->pos[0] - a->pos[0];
                    e1y = b->pos[1] - a->pos[1];
                    e1z = b->pos[2] - a->pos[2];
                    e2x = c->pos[0] - a->pos[0];
                    e2y = c->pos[1] - a->pos[1];
                    e2z = c->pos[2] - a->pos[2];
                    nz = e1x * e2y - e1y * e2x;
                    tris++;
                    if (nz > 0.f) up++;
                }
                walkable = tris > 0 && up * 2 >= tris;
                if (brush_box_n > 0) {
                    for (bi = 0; bi < brush_box_n; bi++) {
                        if (boxes_overlap(&brush_boxes[bi], bmin, bmax)) {
                            hits_brush = 1;
                            break;
                        }
                    }
                }
            }
            if (is_foliage || is_window) {
                cont = "nonsolid";
            } else if ((cflags & 0x10000) != 0) {
                cont = "playerclip";
            } else if (nonsolid_flag) {
                cont = "nonsolid";
            } else if (is_debris) {
                cont = "solid";
            } else if (brushes_emitted == 0) {
                if (walkable || solid_bits) cont = "solid";
            } else if (!hits_brush && (walkable || solid_bits)) {
                /* The draw mesh is the floor or a solid patch (e.g. cylinder). A mesh that touches a brush is detail. */
                cont = "solid";
            }
            if (walkable && (strcmp(cont, "solid") == 0 || strcmp(cont, "playerclip") == 0)) {
                { float nav_a[3] = { bmin[0], bmin[1], bmin[2] - 4.f }; cod_nav_add_walkable_box(&nav_builder, nav_a, bmax); }
            }

            sb_append(&sb, "mesh {\n");
            model_tags(&sb,models,visible_models,linked_models,model_count,s,1);
            sb_append_f(&sb, "  material \"%s\"\n", mat_name);
            sb_append_f(&sb,"  surfaceFlags %d\n",materials && soup->material_id<num_materials?(int)materials[soup->material_id].flags:0);
            sb_append_f(&sb, "  contents %s\n", cont);
            if(soup->lightmap_index>=0 && (unsigned int)soup->lightmap_index<lumps[LUMP_LIGHTMAPS].length/(512u*512u*3u))
                sb_append_f(&sb,"  lightmap \"codlightmaps/%s/%d.tga\"\n",mname,(int)soup->lightmap_index);

            for (v = 0; v < soup->vertex_count; v++) {
                const CoD1Vertex* vert = &verts[soup->vertex_offset + v];
                float tu = vert->uv[0];
                float tv = vert->uv[1];
                /* Z is up, so a water plane is continuous in world XY, not per-face 0..1. */
                if (name_has(mat_name, "water")) {
                    tu = vert->pos[0] * (1.f / 192.f);
                    tv = vert->pos[1] * (1.f / 192.f);
                }
                const unsigned char* rgba=(const unsigned char*)&vert->color;
                sb_append_f(&sb, "  v %.1f %.1f %.1f %.4f %.4f %.2f %.2f %.2f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                            vert->pos[0], vert->pos[1], vert->pos[2],
                            tu, tv,
                            vert->normal[0], vert->normal[1], vert->normal[2],vert->lightmap[0],vert->lightmap[1],
                            rgba[0]/255.0,rgba[1]/255.0,rgba[2]/255.0,rgba[3]/255.0);
            }

            for (t = 0; t + 2 < soup->triangle_count; t += 3) {
                unsigned short i0 = indices[soup->triangle_offset + t];
                unsigned short i1 = indices[soup->triangle_offset + t + 1];
                unsigned short i2 = indices[soup->triangle_offset + t + 2];
                if (i0 < soup->vertex_count && i1 < soup->vertex_count && i2 < soup->vertex_count) {
                    sb_append_f(&sb, "  f %u %u %u\n", (unsigned int)i0, (unsigned int)i2, (unsigned int)i1);
                }
            }

            sb_append(&sb, "}\n\n");
            meshes_emitted++;
        }

        /* If no meshes and no brushes were emitted, fallback to a walkable floor brush */
        if (meshes_emitted == 0 && brushes_emitted == 0) {
            float fmin_x = min_world[0] - 256.f;
            float fmin_y = min_world[1] - 256.f;
            float fmax_x = max_world[0] + 256.f;
            float fmax_y = max_world[1] + 256.f;
            float fz = min_world[2] - 16.f;

            sb_append(&sb, "brush {\n");
            sb_append_f(&sb, "  mins %.1f %.1f %.1f\n", fmin_x, fmin_y, fz - 32.f);
            sb_append_f(&sb, "  maxs %.1f %.1f %.1f\n", fmax_x, fmax_y, fz);
            sb_append(&sb, "  material \"textures/dev/floor\"\n");
            sb_append(&sb, "  contents solid\n");
            sb_append(&sb, "}\n\n");
            { float nav_a[3] = { fmin_x, fmin_y, fz - 32.f }, nav_b[3] = { fmax_x, fmax_y, fz }; cod_nav_add_walkable_box(&nav_builder, nav_a, nav_b); }
        }
    }

    /* Draw soups stay visual. Retail collision for the ground is these patches. */
    emit_terrain_collision(&sb, bytes, size, lumps, materials, num_materials);

    {
        size_t nav_len = 0;
        char* nav_text = cod_nav_emit_hxmap(&nav_builder, &nav_len);
        if (nav_text) {
            sb_append(&sb, nav_text);
            free(nav_text);
        }
    }
    cod_nav_free(&nav_builder);

    free(visible_models);
    free(linked_models);
    free(trigger_models);
    free(model_origins);
    free(brush_boxes);
    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
