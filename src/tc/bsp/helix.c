/*
 * True Combat / Enemy Territory IBSP v47 Map Loader & .hxmap Converter
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bsp/helix.h"
#include "bsp/read.h"
#include "tc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#pragma pack(push, 1)

typedef struct {
    float normal[3];
    float dist;
} TcPlane;

typedef struct {
    char name[64];
    unsigned int surface_flags;
    unsigned int content_flags;
} TcShader;

typedef struct {
    float mins[3];
    float maxs[3];
    int first_surf;
    int num_surfs;
    int first_brush;
    int num_brushes;
} TcModel;

typedef struct {
    int first_side;
    int num_sides;
    int shader_num;
} TcBrush;

typedef struct {
    int plane_num;
    int shader_num;
} TcBrushSide;

typedef struct {
    float pos[3];
    float uv[2];
    float lightmap[2];
    float normal[3];
    unsigned char color[4];
} TcVertex;

typedef struct {
    int shader_num;
    int fog_num;
    int surface_type;
    int first_vert;
    int num_verts;
    int first_index;
    int num_indexes;
    int lightmap_styles[4];
    int vertex_styles[4];
    int lightmap_num[4];
    int lightmap_x[4];
    int lightmap_y[4];
    int lightmap_size[2];
    float lightmap_vecs[3][3];
    int patch_width;
    int patch_height;
} TcSurface;

#pragma pack(pop)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

static void sb_init(StringBuilder* sb) {
    sb->cap = 2 * 1024 * 1024; /* 2 MB initial capacity */
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (sb->data) sb->data[0] = '\0';
}

static void sb_append(StringBuilder* sb, const char* str) {
    size_t slen;
    if (!sb || !sb->data || !str) return;
    slen = strlen(str);
    if (sb->len + slen + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 1024;
        char* grown;
        while (sb->len + slen + 1 > ncap) ncap *= 2;
        grown = (char*)realloc(sb->data, ncap);
        if (!grown) return;
        sb->data = grown;
        sb->cap = ncap;
    }
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void sb_append_f(StringBuilder* sb, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sb_append(sb, buf);
}

static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static int is_tool_shader(const char* name, unsigned int cflags) {
    if (!name || !name[0]) return 1;
    if (strstr(name, "nodraw") != NULL ||
        strstr(name, "caulk") != NULL ||
        strstr(name, "trigger") != NULL ||
        strstr(name, "hint") != NULL ||
        strstr(name, "skip") != NULL ||
        strstr(name, "portal") != NULL ||
        strstr(name, "lightgrid") != NULL ||
        strstr(name, "origin") != NULL ||
        strstr(name, "clipmissile") != NULL ||
        strstr(name, "clip_nosight") != NULL ||
        strstr(name, "clipmonster") != NULL ||
        strstr(name, "clipfoliage") != NULL ||
        strstr(name, "weapclip") != NULL ||
        strstr(name, "weaponclip") != NULL ||
        strstr(name, "clip_weapon") != NULL ||
        strstr(name, "bulletclip") != NULL ||
        strstr(name, "clipshot") != NULL ||
        strstr(name, "sky") != NULL ||
        strstr(name, "corona") != NULL ||
        strstr(name, "flare") != NULL ||
        strstr(name, "sprite") != NULL ||
        (cflags & 0x40000000) != 0 /* TRIGGER */ ||
        (cflags & 0x20000) != 0 /* MONSTERCLIP */) {
        return 1;
    }
    return 0;
}

int bsp_to_helix_map(const unsigned char* bytes, size_t size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size) {
    BspLump lumps[BSP_LUMP_COUNT];
    StringBuilder sb;
    const char* ent_text;
    const char* ent_end;
    const char* p;
    float min_world[3] = { 1e9f, 1e9f, 1e9f };
    float max_world[3] = { -1e9f, -1e9f, -1e9f };
    int spawn_count = 0;
    int brushes_emitted = 0;
    int meshes_emitted = 0;

    const TcShader* shaders = NULL;
    unsigned int num_shaders = 0;
    const TcPlane* planes = NULL;
    unsigned int num_planes = 0;
    const TcModel* models = NULL;
    unsigned int num_models = 0;
    const TcBrush* brushes = NULL;
    unsigned int num_brushes = 0;
    const TcBrushSide* brushsides = NULL;
    unsigned int num_brushsides = 0;
    const TcVertex* verts = NULL;
    unsigned int num_verts = 0;
    const int* indices = NULL;
    unsigned int num_indices = 0;
    const TcSurface* surfaces = NULL;
    unsigned int num_surfaces = 0;

    unsigned int world_first_surf = 0;
    unsigned int world_num_surfs = 0;
    unsigned int world_first_brush = 0;
    unsigned int world_num_brushes = 0;

    if (!bytes || size < 144 || !out_buf || !out_size) return 0;
    if (!bsp_read_lumps(bytes, size, lumps)) return 0;

    /* Setup lump pointers */
    if (lumps[BSP_LUMP_SHADERS].length >= sizeof(TcShader)) {
        shaders = (const TcShader*)(bytes + lumps[BSP_LUMP_SHADERS].offset);
        num_shaders = lumps[BSP_LUMP_SHADERS].length / sizeof(TcShader);
    }
    if (lumps[BSP_LUMP_PLANES].length >= sizeof(TcPlane)) {
        planes = (const TcPlane*)(bytes + lumps[BSP_LUMP_PLANES].offset);
        num_planes = lumps[BSP_LUMP_PLANES].length / sizeof(TcPlane);
    }
    if (lumps[BSP_LUMP_MODELS].length >= sizeof(TcModel)) {
        models = (const TcModel*)(bytes + lumps[BSP_LUMP_MODELS].offset);
        num_models = lumps[BSP_LUMP_MODELS].length / sizeof(TcModel);
        if (num_models > 0) {
            world_first_surf = (unsigned int)models[0].first_surf;
            world_num_surfs = (unsigned int)models[0].num_surfs;
            world_first_brush = (unsigned int)models[0].first_brush;
            world_num_brushes = (unsigned int)models[0].num_brushes;
            min_world[0] = models[0].mins[0];
            min_world[1] = models[0].mins[1];
            min_world[2] = models[0].mins[2];
            max_world[0] = models[0].maxs[0];
            max_world[1] = models[0].maxs[1];
            max_world[2] = models[0].maxs[2];
        }
    }
    if (lumps[BSP_LUMP_BRUSHES].length >= sizeof(TcBrush)) {
        brushes = (const TcBrush*)(bytes + lumps[BSP_LUMP_BRUSHES].offset);
        num_brushes = lumps[BSP_LUMP_BRUSHES].length / sizeof(TcBrush);
    }
    if (lumps[BSP_LUMP_BRUSHSIDES].length >= sizeof(TcBrushSide)) {
        brushsides = (const TcBrushSide*)(bytes + lumps[BSP_LUMP_BRUSHSIDES].offset);
        num_brushsides = lumps[BSP_LUMP_BRUSHSIDES].length / sizeof(TcBrushSide);
    }
    if (lumps[BSP_LUMP_DRAWVERTS].length >= sizeof(TcVertex)) {
        verts = (const TcVertex*)(bytes + lumps[BSP_LUMP_DRAWVERTS].offset);
        num_verts = lumps[BSP_LUMP_DRAWVERTS].length / sizeof(TcVertex);
    }
    if (lumps[BSP_LUMP_DRAWINDEXES].length >= sizeof(int)) {
        indices = (const int*)(bytes + lumps[BSP_LUMP_DRAWINDEXES].offset);
        num_indices = lumps[BSP_LUMP_DRAWINDEXES].length / sizeof(int);
    }
    if (lumps[BSP_LUMP_SURFACES].length >= sizeof(TcSurface)) {
        surfaces = (const TcSurface*)(bytes + lumps[BSP_LUMP_SURFACES].offset);
        num_surfaces = lumps[BSP_LUMP_SURFACES].length / sizeof(TcSurface);
    }

    if (world_num_surfs == 0 || world_num_surfs > num_surfaces) {
        world_num_surfs = num_surfaces;
    }
    if (world_num_brushes == 0 || world_num_brushes > num_brushes) {
        world_num_brushes = num_brushes;
    }

    sb_init(&sb);
    if (!sb.data) return 0;

    /* Map header */
    sb_append(&sb, "map {\n  version 1\n");
    if (map_name && map_name[0]) {
        sb_append_f(&sb, "  name \"%s\"\n", map_name);
    }
    sb_append(&sb, "}\n\n");

    /* 1. Parse Entities */
    if (lumps[BSP_LUMP_ENTITIES].length > 0) {
        ent_text = (const char*)(bytes + lumps[BSP_LUMP_ENTITIES].offset);
        ent_end = ent_text + lumps[BSP_LUMP_ENTITIES].length;
        p = ent_text;

        while (p < ent_end) {
            const char* brace = strchr(p, '{');
            const char* close_brace;
            const char* line_ptr;
            char classname[128] = "";
            char origin[128] = "";
            char angle[64] = "";
            char angles[64] = "";
            char message[256] = "";

            if (!brace || brace >= ent_end) break;
            close_brace = strchr(brace, '}');
            if (!close_brace || close_brace >= ent_end) break;

            line_ptr = brace + 1;
            while (line_ptr < close_brace) {
                const char* q1 = strchr(line_ptr, '\"');
                const char* q2;
                const char* q3;
                const char* q4;
                char key[128] = "";
                char val[256] = "";
                size_t klen, vlen;

                if (!q1 || q1 >= close_brace) break;
                q2 = strchr(q1 + 1, '\"');
                if (!q2 || q2 >= close_brace) break;
                q3 = strchr(q2 + 1, '\"');
                if (!q3 || q3 >= close_brace) break;
                q4 = strchr(q3 + 1, '\"');
                if (!q4 || q4 >= close_brace) break;

                klen = (size_t)(q2 - q1 - 1);
                vlen = (size_t)(q4 - q3 - 1);
                if (klen >= sizeof(key)) klen = sizeof(key) - 1;
                if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                memcpy(key, q1 + 1, klen);
                key[klen] = '\0';
                memcpy(val, q3 + 1, vlen);
                val[vlen] = '\0';

                if (tc_strcasecmp(key, "classname") == 0) {
                    snprintf(classname, sizeof(classname), "%s", val);
                } else if (tc_strcasecmp(key, "origin") == 0) {
                    snprintf(origin, sizeof(origin), "%s", val);
                } else if (tc_strcasecmp(key, "angle") == 0) {
                    snprintf(angle, sizeof(angle), "%s", val);
                } else if (tc_strcasecmp(key, "angles") == 0) {
                    snprintf(angles, sizeof(angles), "%s", val);
                } else if (tc_strcasecmp(key, "message") == 0) {
                    snprintf(message, sizeof(message), "%s", val);
                }

                line_ptr = q4 + 1;
            }

            if (tc_strcasecmp(classname, "worldspawn") == 0) {
                sb_append(&sb, "entity {\n  \"classname\" \"worldspawn\"\n");
                if (message[0]) {
                    sb_append_f(&sb, "  \"message\" \"%s\"\n", message);
                }
                sb_append(&sb, "}\n\n");
            } else if (tc_strcasecmp(classname, "info_player_deathmatch") == 0 ||
                       tc_strcasecmp(classname, "info_player_start") == 0 ||
                       tc_strcasecmp(classname, "team_CTF_redspawn") == 0 ||
                       tc_strcasecmp(classname, "team_CTF_bluespawn") == 0 ||
                       tc_strcasecmp(classname, "team_CTF_redplayer") == 0 ||
                       tc_strcasecmp(classname, "team_CTF_blueplayer") == 0) {
                float ox = 0, oy = 0, oz = 0;
                if (sscanf(origin, "%f %f %f", &ox, &oy, &oz) == 3) {
                    oz += 16.0f; /* Clear ground plane */
                    sb_append(&sb, "entity {\n  \"classname\" \"info_player_deathmatch\"\n");
                    sb_append_f(&sb, "  \"origin\" \"%.1f %.1f %.1f\"\n", ox, oy, oz);
                    if (angles[0]) {
                        sb_append_f(&sb, "  \"angles\" \"%s\"\n", angles);
                    } else if (angle[0]) {
                        sb_append_f(&sb, "  \"angle\" \"%s\"\n", angle);
                    }
                    sb_append(&sb, "}\n\n");
                    spawn_count++;
                }
            } else if (tc_strcasecmp(classname, "info_player_intermission") == 0) {
                sb_append(&sb, "entity {\n  \"classname\" \"info_player_intermission\"\n");
                if (origin[0]) sb_append_f(&sb, "  \"origin\" \"%s\"\n", origin);
                if (angles[0]) sb_append_f(&sb, "  \"angles\" \"%s\"\n", angles);
                sb_append(&sb, "}\n\n");
            }

            p = close_brace + 1;
        }
    }

    /* Fallback spawn if none found */
    if (spawn_count == 0) {
        float cx = 0, cy = 0, cz = 32.f;
        if (min_world[0] < max_world[0]) {
            cx = (min_world[0] + max_world[0]) * 0.5f;
            cy = (min_world[1] + max_world[1]) * 0.5f;
            cz = min_world[2] + 48.0f;
        }
        sb_append(&sb, "entity {\n  \"classname\" \"info_player_deathmatch\"\n");
        sb_append_f(&sb, "  \"origin\" \"%.1f %.1f %.1f\"\n", cx, cy, cz);
        sb_append(&sb, "}\n\n");
    }

    /* 2. Emit Brushes from Model 0 */
    if (brushes && brushsides && planes && world_num_brushes > 0) {
        unsigned int b_idx;
        for (b_idx = 0; b_idx < world_num_brushes; b_idx++) {
            const TcBrush* b = &brushes[world_first_brush + b_idx];
            const TcShader* sh;
            const char* mat_name = "textures/common/nodraw";
            unsigned int cflags = 0;
            int s;

            if (b->num_sides < 4) continue;
            if (b->first_side < 0 || (unsigned int)b->first_side + (unsigned int)b->num_sides > num_brushsides) continue;

            if (shaders && b->shader_num >= 0 && (unsigned int)b->shader_num < num_shaders) {
                sh = &shaders[b->shader_num];
                mat_name = sh->name;
                cflags = sh->content_flags;
            }

            /* Tool shader filtering */
            if (is_tool_shader(mat_name, cflags)) {
                /* If not a solid playerclip, skip triggers/hints/caulks */
                if (!(cflags & 0x10001)) continue;
            }

            sb_append(&sb, "brush {\n");
            for (s = 0; s < b->num_sides; s++) {
                int p_num = brushsides[b->first_side + s].plane_num;
                if (p_num >= 0 && (unsigned int)p_num < num_planes) {
                    const TcPlane* pl = &planes[p_num];
                    sb_append_f(&sb, "  plane %.5f %.5f %.5f %.2f\n",
                                pl->normal[0], pl->normal[1], pl->normal[2], pl->dist);
                }
            }
            sb_append_f(&sb, "  material \"%s\"\n", mat_name);
            sb_append(&sb, "  contents solid\n");
            sb_append(&sb, "}\n\n");
            brushes_emitted++;
        }
    }

    /* 3. Emit Surfaces / Meshes from Model 0 */
    if (surfaces && verts && indices && world_num_surfs > 0) {
        unsigned int s_idx;
        for (s_idx = 0; s_idx < world_num_surfs; s_idx++) {
            const TcSurface* surf = &surfaces[world_first_surf + s_idx];
            const char* mat_name = "textures/dev/floor";
            unsigned int cflags = 0;
            const char* cont = "nonsolid";
            int is_foliage = 0;
            int is_terrain = 0;
            int v, t;

            /* Only planar (1) and triangle soup (3) surfaces are emitted as indexed meshes */
            if (surf->surface_type != 1 && surf->surface_type != 3) continue;
            if (surf->num_verts < 3 || surf->num_indexes < 3) continue;
            if (surf->first_vert < 0 || (unsigned int)surf->first_vert + (unsigned int)surf->num_verts > num_verts) continue;
            if (surf->first_index < 0 || (unsigned int)surf->first_index + (unsigned int)surf->num_indexes > num_indices) continue;

            if (shaders && surf->shader_num >= 0 && (unsigned int)surf->shader_num < num_shaders) {
                mat_name = shaders[surf->shader_num].name;
                cflags = shaders[surf->shader_num].content_flags;
            }

            if (is_tool_shader(mat_name, cflags)) continue;

            is_foliage = (strstr(mat_name, "foliage") != NULL ||
                          strstr(mat_name, "bush") != NULL ||
                          strstr(mat_name, "tree") != NULL ||
                          strstr(mat_name, "plant") != NULL ||
                          strstr(mat_name, "vine") != NULL);

            if (!is_foliage) {
                if (strstr(mat_name, "snow") != NULL ||
                    strstr(mat_name, "terrain") != NULL ||
                    strstr(mat_name, "ground") != NULL ||
                    strstr(mat_name, "dirt") != NULL ||
                    strstr(mat_name, "road") != NULL) {
                    is_terrain = 1;
                }
            }

            if (brushes_emitted == 0) {
                if (!is_foliage) cont = "solid";
            } else if (is_terrain) {
                cont = "solid";
            }

            sb_append(&sb, "mesh {\n");
            sb_append_f(&sb, "  material \"%s\"\n", mat_name);
            sb_append_f(&sb, "  contents %s\n", cont);

            for (v = 0; v < surf->num_verts; v++) {
                const TcVertex* vert = &verts[surf->first_vert + v];
                sb_append_f(&sb, "  v %.1f %.1f %.1f %.4f %.4f %.2f %.2f %.2f\n",
                            vert->pos[0], vert->pos[1], vert->pos[2],
                            vert->uv[0], vert->uv[1],
                            vert->normal[0], vert->normal[1], vert->normal[2]);
            }

            for (t = 0; t + 2 < surf->num_indexes; t += 3) {
                int i0 = indices[surf->first_index + t];
                int i1 = indices[surf->first_index + t + 1];
                int i2 = indices[surf->first_index + t + 2];
                if (i0 >= 0 && i0 < surf->num_verts &&
                    i1 >= 0 && i1 < surf->num_verts &&
                    i2 >= 0 && i2 < surf->num_verts) {
                    /* Swap winding i1 and i2 for Helix */
                    sb_append_f(&sb, "  f %d %d %d\n", i0, i2, i1);
                }
            }

            sb_append(&sb, "}\n\n");
            meshes_emitted++;
        }
    }

    /* Fallback ground brush if empty */
    if (brushes_emitted == 0 && meshes_emitted == 0) {
        float fmin_x = (min_world[0] < max_world[0]) ? min_world[0] - 256.f : -1024.f;
        float fmin_y = (min_world[1] < max_world[1]) ? min_world[1] - 256.f : -1024.f;
        float fmax_x = (min_world[0] < max_world[0]) ? max_world[0] + 256.f : 1024.f;
        float fmax_y = (min_world[1] < max_world[1]) ? max_world[1] + 256.f : 1024.f;
        float fz = (min_world[2] < max_world[2]) ? min_world[2] - 16.f : 0.f;

        sb_append(&sb, "brush {\n");
        sb_append_f(&sb, "  mins %.1f %.1f %.1f\n", fmin_x, fmin_y, fz - 32.f);
        sb_append_f(&sb, "  maxs %.1f %.1f %.1f\n", fmax_x, fmax_y, fz);
        sb_append(&sb, "  material \"textures/dev/floor\"\n");
        sb_append(&sb, "  contents solid\n");
        sb_append(&sb, "}\n\n");
    }

    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
