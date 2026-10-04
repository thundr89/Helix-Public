/*
 * Call of Duty 1 (2003) IBSP v59 Map Loader & .hxmap Converter
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "cod_bsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

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
#define LUMP_MODELS        27
#define LUMP_ENTITIES      29

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
    unsigned short draw_order;
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
    const unsigned char* p = (const unsigned char*)data;
    unsigned int ident;
    int version;
    if (!data || size < 8 + COD_BSP_LUMPS * 8) return 0;
    ident = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
            ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
    version = (int)((unsigned int)p[4] | ((unsigned int)p[5] << 8) |
                    ((unsigned int)p[6] << 16) | ((unsigned int)p[7] << 24));
    return (ident == COD_BSP_IDENT && version == COD_BSP_VERSION);
}

static int parse_lumps(const unsigned char* data, size_t size, CoDLump* lumps) {
    int i;
    if (!data || size < 8 + COD_BSP_LUMPS * 8) return 0;
    for (i = 0; i < COD_BSP_LUMPS; i++) {
        const unsigned char* p = data + 8 + i * 8;
        lumps[i].length = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
                          ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
        lumps[i].offset = (unsigned int)p[4] | ((unsigned int)p[5] << 8) |
                          ((unsigned int)p[6] << 16) | ((unsigned int)p[7] << 24);
    }
    return 1;
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

int cod_bsp_to_hxmap(const void* data, size_t size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size) {
    const unsigned char* bytes = (const unsigned char*)data;
    CoDLump lumps[COD_BSP_LUMPS];
    StringBuilder sb;
    const char* mname = (map_name && map_name[0]) ? map_name : "cod_map";
    float min_world[3] = {  999999.f,  999999.f,  999999.f };
    float max_world[3] = { -999999.f, -999999.f, -999999.f };
    int spawn_count = 0;

    if (!cod_bsp_is_valid(data, size)) return 0;
    if (!parse_lumps(bytes, size, lumps)) return 0;

    sb_init(&sb);
    if (!sb.data) return 0;

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
                if (p < end && *p == '}') p++;

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

                    /* Emit player spawn entity */
                    if (strcmp(mapped_class, "info_player_deathmatch") == 0 ||
                        strcmp(mapped_class, "info_player_start") == 0) {
                        sb_append(&sb, "entity {\n");
                        sb_append_f(&sb, "  \"classname\" \"%s\"\n", mapped_class);
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

                    /* Static world props. Brush models ("*0") stay in the BSP soup. */
                    if ((strcmp(classname, "misc_model") == 0 || strcmp(classname, "script_model") == 0) &&
                        model[0] && model[0] != '*') {
                        /* Skip invisible particle emitters and destroyed model variants */
                        if (strcmp(model, "xmodel/fx") == 0) {
                            /* Invisible particle fx anchor */
                        } else if (strstr(model, "_d") != NULL && strstr(model, "_d1") == NULL) {
                            /* Destroyed state - active only after bomb explosion */
                        } else {
                            sb_append(&sb, "entity {\n");
                            sb_append(&sb, "  \"classname\" \"misc_model\"\n");
                            sb_append_f(&sb, "  \"model\" \"%s\"\n", model);
                            if (origin[0]) sb_append_f(&sb, "  \"origin\" \"%s\"\n", origin);
                            if (angles[0]) sb_append_f(&sb, "  \"angles\" \"%s\"\n", angles);
                            else if (angle[0]) sb_append_f(&sb, "  \"angle\" \"%s\"\n", angle);
                            if (modelscale[0]) sb_append_f(&sb, "  \"modelscale\" \"%s\"\n", modelscale);
                            sb_append(&sb, "}\n\n");
                        }
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
    }

    /* 2. Parse Brushes (Lump 4), BrushSides (Lump 3), Planes (Lump 2), Materials (Lump 0) */
    int brushes_emitted = 0;
    unsigned int num_materials = 0;
    const CoDMaterial* materials = NULL;
    if (lumps[LUMP_MATERIALS].length > 0) {
        num_materials = lumps[LUMP_MATERIALS].length / sizeof(CoDMaterial);
        materials = (const CoDMaterial*)(bytes + lumps[LUMP_MATERIALS].offset);
    }

    if (lumps[LUMP_BRUSHES].length > 0 &&
        lumps[LUMP_BRUSHSIDES].length > 0) {

        unsigned int num_brushes = lumps[LUMP_BRUSHES].length / sizeof(CoD1Brush);
        if (lumps[LUMP_MODELS].length >= sizeof(CoD1Model)) {
            const CoD1Model* world_model = (const CoD1Model*)(bytes + lumps[LUMP_MODELS].offset);
            if (world_model->num_brushes > 0 && (unsigned int)world_model->num_brushes <= num_brushes) {
                num_brushes = (unsigned int)world_model->num_brushes;
            }
        }
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
                if (materials && brush->material_id < num_materials) {
                    if (materials[brush->material_id].name[0]) {
                        mat_name = materials[brush->material_id].name;
                    }
                    cflags = materials[brush->material_id].content_flags;
                }

                int is_tool = (strstr(mat_name, "trigger") != NULL ||
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
                               strstr(mat_name, "occluder") != NULL ||
                               (cflags & 0x40000000) != 0 /* TRIGGER */);

                if (!is_tool) {
                    float d0 = brushsides[side_idx + 0].column1.dist;
                    float d1 = brushsides[side_idx + 1].column1.dist;
                    float d2 = brushsides[side_idx + 2].column1.dist;
                    float d3 = brushsides[side_idx + 3].column1.dist;
                    float d4 = brushsides[side_idx + 4].column1.dist;
                    float d5 = brushsides[side_idx + 5].column1.dist;

                    float min_x = (d0 < d1) ? d0 : d1;
                    float max_x = (d0 < d1) ? d1 : d0;
                    float min_y = (d2 < d3) ? d2 : d3;
                    float max_y = (d2 < d3) ? d3 : d2;
                    float min_z = (d4 < d5) ? d4 : d5;
                    float max_z = (d4 < d5) ? d5 : d4;

                    /* Skip degenerate zero-volume brushes */
                    if ((max_x - min_x) >= 0.1f && (max_y - min_y) >= 0.1f && (max_z - min_z) >= 0.1f) {
                        sb_append(&sb, "brush {\n");
                        sb_append_f(&sb, "  mins %.1f %.1f %.1f\n", min_x, min_y, min_z);
                        sb_append_f(&sb, "  maxs %.1f %.1f %.1f\n", max_x, max_y, max_z);

                        if (num_sides > 6 && planes) {
                            unsigned int s;
                            sb_append_f(&sb, "  plane -1.0 0.0 0.0 %.1f\n", -min_x);
                            sb_append_f(&sb, "  plane 1.0 0.0 0.0 %.1f\n", max_x);
                            sb_append_f(&sb, "  plane 0.0 -1.0 0.0 %.1f\n", -min_y);
                            sb_append_f(&sb, "  plane 0.0 1.0 0.0 %.1f\n", max_y);
                            sb_append_f(&sb, "  plane 0.0 0.0 -1.0 %.1f\n", -min_z);
                            sb_append_f(&sb, "  plane 0.0 0.0 1.0 %.1f\n", max_z);
                            for (s = 6; s < num_sides; s++) {
                                unsigned int p_idx = brushsides[side_idx + s].column1.plane;
                                if (p_idx < num_planes) {
                                    const CoD1Plane* pl = &planes[p_idx];
                                    sb_append_f(&sb, "  plane %.4f %.4f %.4f %.2f\n",
                                                pl->normal[0], pl->normal[1], pl->normal[2], pl->dist);
                                }
                            }
                        }

                        sb_append(&sb, "  material \"textures/common/nodraw\"\n");
                        sb_append(&sb, "  contents solid\n");
                        sb_append(&sb, "}\n\n");
                        brushes_emitted++;
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
        if (lumps[LUMP_MODELS].length >= sizeof(CoD1Model)) {
            const CoD1Model* world_model = (const CoD1Model*)(bytes + lumps[LUMP_MODELS].offset);
            if (world_model->num_surfs > 0 && (unsigned int)world_model->num_surfs <= num_soups) {
                num_soups = (unsigned int)world_model->num_surfs;
            }
        }
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
            int is_decal = (strstr(mat_name, "decal") != NULL && strstr(mat_name, "window") == NULL);
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
                           is_decal ||
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
                           (cflags & 0x40000000) != 0 /* TRIGGER */ ||
                           (cflags & 0x20000) != 0 /* MONSTERCLIP */);
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
            if (brushes_emitted == 0) {
                if (!is_foliage && !((cflags & 0x20000000) && !(cflags & 0x10001))) {
                    cont = "solid";
                }
            } else {
                int is_solid_structure = 0;
                if (!is_foliage) {
                    if (strstr(mat_name, "ground") != NULL ||
                        strstr(mat_name, "terrain") != NULL ||
                        strstr(mat_name, "dirt") != NULL ||
                        strstr(mat_name, "grass") != NULL ||
                        strstr(mat_name, "snow") != NULL ||
                        strstr(mat_name, "mud") != NULL ||
                        strstr(mat_name, "sand") != NULL ||
                        strstr(mat_name, "gravel") != NULL ||
                        strstr(mat_name, "path") != NULL ||
                        strstr(mat_name, "road") != NULL ||
                        strstr(mat_name, "wood") != NULL ||
                        strstr(mat_name, "plank") != NULL ||
                        strstr(mat_name, "board") != NULL ||
                        strstr(mat_name, "ramp") != NULL ||
                        strstr(mat_name, "catwalk") != NULL ||
                        strstr(mat_name, "stair") != NULL ||
                        strstr(mat_name, "step") != NULL ||
                        strstr(mat_name, "bridge") != NULL ||
                        strstr(mat_name, "dock") != NULL ||
                        strstr(mat_name, "pier") != NULL ||
                        strstr(mat_name, "floor") != NULL) {
                        is_solid_structure = 1;
                    }
                }
                if (is_solid_structure) {
                    cont = "solid";
                }
            }

            sb_append(&sb, "mesh {\n");
            sb_append_f(&sb, "  material \"%s\"\n", mat_name);
            sb_append_f(&sb, "  contents %s\n", cont);

            for (v = 0; v < soup->vertex_count; v++) {
                const CoD1Vertex* vert = &verts[soup->vertex_offset + v];
                sb_append_f(&sb, "  v %.1f %.1f %.1f %.4f %.4f %.2f %.2f %.2f\n",
                            vert->pos[0], vert->pos[1], vert->pos[2],
                            vert->uv[0], vert->uv[1],
                            vert->normal[0], vert->normal[1], vert->normal[2]);
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
        }
    }

    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
