#include "cs16_bsp.h"
#include "cs16_wad.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOLDSRC_BSP_VERSION 30

enum {
    LUMP_ENTITIES = 0,
    LUMP_PLANES = 1,
    LUMP_TEXTURES = 2,
    LUMP_VERTEXES = 3,
    LUMP_VISIBILITY = 4,
    LUMP_NODES = 5,
    LUMP_TEXINFO = 6,
    LUMP_FACES = 7,
    LUMP_LIGHTING = 8,
    LUMP_CLIPNODES = 9,
    LUMP_LEAFS = 10,
    LUMP_MARKSURFACES = 11,
    LUMP_EDGES = 12,
    LUMP_SURFEDGES = 13,
    LUMP_MODELS = 14,
    HEADER_LUMPS = 15
};

#pragma pack(push, 1)
typedef struct {
    int offset;
    int length;
} BspLump;

typedef struct {
    int version;
    BspLump lumps[HEADER_LUMPS];
} BspHeader;

typedef struct {
    float normal[3];
    float dist;
    int type;
} BspPlane;

typedef struct {
    float point[3];
} BspVertex;

typedef struct {
    unsigned short v[2];
} BspEdge;

typedef struct {
    float vecs[2][4];
    int miptex;
    int flags;
} BspTexInfo;

typedef struct {
    short planenum;
    short side;
    int firstedge;
    short numedges;
    short texinfo;
    unsigned char styles[4];
    int lightofs;
} BspFace;

typedef struct {
    float mins[3];
    float maxs[3];
    float origin[3];
    int headnode[4];
    int visleafs;
    int firstface;
    int numfaces;
} BspModel;

typedef struct {
    char name[16];
    unsigned int width;
    unsigned int height;
    unsigned int offsets[4];
} BspMipTex;
#pragma pack(pop)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_init(StrBuf* sb, size_t initial_cap) {
    sb->cap = initial_cap ? initial_cap : 65536;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) return 0;
    sb->data[0] = '\0';
    return 1;
}

static int sb_append_n(StrBuf* sb, const char* s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        while (new_cap < sb->len + n + 1) new_cap *= 2;
        char* p = (char*)realloc(sb->data, new_cap);
        if (!p) return 0;
        sb->data = p;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_appendf(StrBuf* sb, const char* fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(tmp)) {
        return sb_append_n(sb, tmp, (size_t)n);
    }
    char* big = (char*)malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = sb_append_n(sb, big, (size_t)n);
    free(big);
    return ok;
}

static int lump_ok(const BspHeader* hdr, size_t total, int idx, size_t elem_size, unsigned int* out_count) {
    const BspLump* l = &hdr->lumps[idx];
    if (l->offset < 0 || l->length < 0) return 0;
    if ((size_t)l->offset + (size_t)l->length > total) return 0;
    if (elem_size > 0 && out_count) {
        *out_count = (unsigned int)((size_t)l->length / elem_size);
    }
    return 1;
}

static void sanitize_texname(const char* in, char* out, size_t cap) {
    char clean[64];
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < sizeof(clean); ++i) {
        unsigned char uc = (unsigned char)in[i];
        char c = (char)tolower(uc);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
        clean[j++] = c;
    }
    clean[j] = '\0';
    if (j == 0) snprintf(out, cap, "common/caulk");
    else snprintf(out, cap, "textures/%s", clean);
}

static void get_miptex_info(const unsigned char* bsp_data, const BspHeader* hdr,
                            int miptex_idx, char* out_name, size_t cap,
                            unsigned int* out_w, unsigned int* out_h) {
    snprintf(out_name, cap, "common/caulk");
    if (out_w) *out_w = 16;
    if (out_h) *out_h = 16;
    const BspLump* tl = &hdr->lumps[LUMP_TEXTURES];
    if (tl->length < 4 || miptex_idx < 0) return;
    const unsigned char* base = bsp_data + tl->offset;
    int count = 0;
    memcpy(&count, base, 4);
    if (miptex_idx >= count) return;
    if (4 + (miptex_idx + 1) * 4 > tl->length) return;
    int ofs = 0;
    memcpy(&ofs, base + 4 + miptex_idx * 4, 4);
    if (ofs < 0 || ofs + (int)sizeof(BspMipTex) > tl->length) return;
    const BspMipTex* mt = (const BspMipTex*)(base + ofs);
    char raw[17];
    memcpy(raw, mt->name, 16);
    raw[16] = '\0';
    sanitize_texname(raw, out_name, cap);
    if (out_w) *out_w = mt->width ? mt->width : 16;
    if (out_h) *out_h = mt->height ? mt->height : 16;
}

int cs16_bsp_extract_embedded_texture(const unsigned char* bsp_data, size_t bsp_size,
                                      const char* sanitized_name,
                                      unsigned char** out_buf, unsigned int* out_size) {
    if (!bsp_data || bsp_size < sizeof(BspHeader) || !sanitized_name || !out_buf || !out_size) return 0;
    const BspHeader* hdr = (const BspHeader*)bsp_data;
    if (hdr->version != GOLDSRC_BSP_VERSION) return 0;
    const BspLump* tl = &hdr->lumps[LUMP_TEXTURES];
    if (tl->offset < 0 || tl->length < 4 || (size_t)tl->offset + (size_t)tl->length > bsp_size) return 0;

    const unsigned char* base = bsp_data + tl->offset;
    int count = 0;
    memcpy(&count, base, 4);
    for (int i = 0; i < count; ++i) {
        if (4 + (i + 1) * 4 > tl->length) break;
        int ofs = 0;
        memcpy(&ofs, base + 4 + i * 4, 4);
        if (ofs < 0 || ofs + (int)sizeof(BspMipTex) > tl->length) continue;
        const BspMipTex* mt = (const BspMipTex*)(base + ofs);
        if (mt->offsets[0] == 0) continue; /* Not embedded */

        char clean[64];
        char raw[17];
        memcpy(raw, mt->name, 16);
        raw[16] = '\0';
        size_t j = 0;
        for (size_t k = 0; raw[k] && j + 1 < sizeof(clean); ++k) {
            unsigned char uc = (unsigned char)raw[k];
            char c = (char)tolower(uc);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
            clean[j++] = c;
        }
        clean[j] = '\0';

        if (strcmp(clean, sanitized_name) == 0) {
            const unsigned char* lump_ptr = base + ofs;
            size_t max_lump_sz = (size_t)(tl->length - ofs);
            return cs16_wad3_miptex_to_tga(lump_ptr, max_lump_sz, raw, out_buf, out_size);
        }
    }
    return 0;
}

static void parse_skyname_from_worldspawn(const char* ent_text, size_t ent_len, char* out_sky, size_t cap) {
    snprintf(out_sky, cap, "des"); /* Default fallback */
    const char* p = ent_text;
    const char* end = ent_text + ent_len;
    while (p < end && *p != '{') p++;
    if (p >= end) return;
    const char* w_end = p;
    while (w_end < end && *w_end != '}') w_end++;

    const char* cur = p;
    while (cur < w_end) {
        while (cur < w_end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) cur++;
        if (cur >= w_end || *cur != '"') { cur++; continue; }
        cur++;
        const char* ks = cur;
        while (cur < w_end && *cur != '"') cur++;
        size_t kl = (size_t)(cur - ks);
        if (cur < w_end) cur++;

        while (cur < w_end && *cur != '"') cur++;
        if (cur >= w_end) break;
        cur++;
        const char* vs = cur;
        while (cur < w_end && *cur != '"') cur++;
        size_t vl = (size_t)(cur - vs);
        if (cur < w_end) cur++;

        if (kl == 7 && strncmp(ks, "skyname", 7) == 0) {
            size_t copy_l = vl < cap - 1 ? vl : cap - 1;
            memcpy(out_sky, vs, copy_l);
            out_sky[copy_l] = '\0';
            /* Lowercase */
            for (size_t i = 0; out_sky[i]; ++i) {
                out_sky[i] = (char)tolower((unsigned char)out_sky[i]);
            }
            return;
        }
    }
}

static void get_visible_models(const char* ent_text, size_t ent_len,
                               unsigned char* is_visible, unsigned int num_models) {
    if (!is_visible || num_models == 0) return;
    for (unsigned int i = 0; i < num_models; ++i) is_visible[i] = 1;

    const char* p = ent_text;
    const char* end = ent_text + ent_len;
    while (p < end) {
        while (p < end && *p != '{') p++;
        if (p >= end) break;
        p++;

        char classname[64] = {0};
        char model_str[32] = {0};

        while (p < end && *p != '}') {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= end || *p == '}') break;
            if (*p != '"') { p++; continue; }
            p++;
            const char* kstart = p;
            while (p < end && *p != '"') p++;
            size_t klen = (size_t)(p - kstart);
            if (p < end) p++;

            while (p < end && *p != '"' && *p != '}') p++;
            if (p >= end || *p == '}') break;
            p++;
            const char* vstart = p;
            while (p < end && *p != '"') p++;
            size_t vlen = (size_t)(p - vstart);
            if (p < end) p++;

            if (klen == 9 && strncmp(kstart, "classname", 9) == 0) {
                size_t c = vlen < sizeof(classname) - 1 ? vlen : sizeof(classname) - 1;
                memcpy(classname, vstart, c);
                classname[c] = '\0';
            } else if (klen == 5 && strncmp(kstart, "model", 5) == 0) {
                size_t c = vlen < sizeof(model_str) - 1 ? vlen : sizeof(model_str) - 1;
                memcpy(model_str, vstart, c);
                model_str[c] = '\0';
            }
        }

        if (model_str[0] == '*') {
            int midx = atoi(model_str + 1);
            if (midx > 0 && (unsigned int)midx < num_models) {
                if (strcmp(classname, "func_ladder") == 0 ||
                    strcmp(classname, "func_bomb_target") == 0 ||
                    strcmp(classname, "func_hostage_rescue") == 0 ||
                    strcmp(classname, "func_buyzone") == 0 ||
                    strcmp(classname, "func_escapezone") == 0 ||
                    strcmp(classname, "func_vip_safetyzone") == 0 ||
                    strcmp(classname, "func_monsterclip") == 0 ||
                    strncmp(classname, "trigger_", 8) == 0) {
                    is_visible[midx] = 0;
                } else if (strcmp(classname, "func_illusionary") == 0) {
                    is_visible[midx] = 2; /* visible but nonsolid */
                }
            }
        }
    }
}

static void emit_entities(StrBuf* sb, const char* ent_text, size_t ent_len,
                          const BspModel* models, unsigned int num_models) {
    const char* p = ent_text;
    const char* end = ent_text + ent_len;

    while (p < end) {
        while (p < end && *p != '{') p++;
        if (p >= end) break;
        p++; /* skip '{' */

        char classname[64] = {0};
        char origin[64] = {0};
        char mins_str[64] = {0};
        char maxs_str[64] = {0};
        char angles[64] = {0};
        char model_str[32] = {0};
        char target[64] = {0};
        char targetname[64] = {0};
        char message[128] = {0};
        int health = 0;
        int spawnflags = 0;

        char keys[32][64];
        char vals[32][128];
        int kv_count = 0;

        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= end || *p == '}') {
                if (p < end) p++;
                break;
            }
            if (*p != '"') {
                p++;
                continue;
            }
            p++;
            const char* kstart = p;
            while (p < end && *p != '"') p++;
            size_t klen = (size_t)(p - kstart);
            if (p < end) p++;

            while (p < end && *p != '"' && *p != '}') p++;
            if (p >= end || *p == '}') break;
            p++;
            const char* vstart = p;
            while (p < end && *p != '"') p++;
            size_t vlen = (size_t)(p - vstart);
            if (p < end) p++;

            if (kv_count < 32) {
                size_t kc = klen < 63 ? klen : 63;
                size_t vc = vlen < 127 ? vlen : 127;
                memcpy(keys[kv_count], kstart, kc);
                keys[kv_count][kc] = '\0';
                memcpy(vals[kv_count], vstart, vc);
                vals[kv_count][vc] = '\0';

                if (strcmp(keys[kv_count], "classname") == 0) {
                    snprintf(classname, sizeof(classname), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "origin") == 0) {
                    snprintf(origin, sizeof(origin), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "angles") == 0) {
                    snprintf(angles, sizeof(angles), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "model") == 0) {
                    snprintf(model_str, sizeof(model_str), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "target") == 0) {
                    snprintf(target, sizeof(target), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "targetname") == 0) {
                    snprintf(targetname, sizeof(targetname), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "message") == 0) {
                    snprintf(message, sizeof(message), "%s", vals[kv_count]);
                } else if (strcmp(keys[kv_count], "health") == 0) {
                    health = atoi(vals[kv_count]);
                } else if (strcmp(keys[kv_count], "spawnflags") == 0) {
                    spawnflags = atoi(vals[kv_count]);
                }
                kv_count++;
            }
        }

        if (strcmp(classname, "worldspawn") == 0) continue;
        if (classname[0] == '\0') continue;

        /* For func_ladder, emit a brush with contents ladder */
        if (strcmp(classname, "func_ladder") == 0 && model_str[0] == '*') {
            int midx = atoi(model_str + 1);
            if (midx > 0 && (unsigned int)midx < num_models) {
                const BspModel* m = &models[midx];
                sb_appendf(sb, "brush {\n  mins %.1f %.1f %.1f\n  maxs %.1f %.1f %.1f\n  contents ladder\n}\n",
                           m->mins[0], m->mins[1], m->mins[2], m->maxs[0], m->maxs[1], m->maxs[2]);
            }
            continue;
        }

        /* Skip static brush models whose faces are already part of the solid mesh world */
        if (strcmp(classname, "func_wall") == 0 || strcmp(classname, "func_illusionary") == 0) {
            continue;
        }
        /* func_breakable with SF_BREAK_TRIGGER_ONLY (1): unbreakable crates (e.g. dust2 bombsite crates) */
        if (strcmp(classname, "func_breakable") == 0 && (spawnflags & 1)) {
            continue;
        }

        /* Compute origin and mins/maxs from brush model if present */
        if (model_str[0] == '*') {
            int midx = atoi(model_str + 1);
            if (midx > 0 && (unsigned int)midx < num_models) {
                const BspModel* m = &models[midx];
                if (origin[0] == '\0') {
                    float cx = 0.5f * (m->mins[0] + m->maxs[0]);
                    float cy = 0.5f * (m->mins[1] + m->maxs[1]);
                    float cz = 0.5f * (m->mins[2] + m->maxs[2]);
                    snprintf(origin, sizeof(origin), "%.1f %.1f %.1f", cx, cy, cz);
                }
                snprintf(mins_str, sizeof(mins_str), "%.1f %.1f %.1f", m->mins[0], m->mins[1], m->mins[2]);
                snprintf(maxs_str, sizeof(maxs_str), "%.1f %.1f %.1f", m->maxs[0], m->maxs[1], m->maxs[2]);
            }
        }

        sb_append_n(sb, "entity {\n", 9);
        sb_appendf(sb, "  \"classname\" \"%s\"\n", classname);
        if (origin[0]) sb_appendf(sb, "  \"origin\" \"%s\"\n", origin);
        if (mins_str[0]) sb_appendf(sb, "  \"mins\" \"%s\"\n", mins_str);
        if (maxs_str[0]) sb_appendf(sb, "  \"maxs\" \"%s\"\n", maxs_str);
        if (angles[0]) sb_appendf(sb, "  \"angles\" \"%s\"\n", angles);
        if (target[0]) sb_appendf(sb, "  \"target\" \"%s\"\n", target);
        if (targetname[0]) sb_appendf(sb, "  \"targetname\" \"%s\"\n", targetname);
        if (message[0]) sb_appendf(sb, "  \"message\" \"%s\"\n", message);
        if (health > 0) sb_appendf(sb, "  \"health\" \"%d\"\n", health);
        if (strcmp(classname, "info_player_start") == 0 ||
            strcmp(classname, "info_player_deathmatch") == 0 ||
            strcmp(classname, "func_bomb_target") == 0 ||
            strcmp(classname, "info_bomb_target") == 0 ||
            strcmp(classname, "hostage_entity") == 0 ||
            strcmp(classname, "monster_hostage") == 0 ||
            strcmp(classname, "func_hostage_rescue") == 0 ||
            strcmp(classname, "info_hostage_rescue") == 0 ||
            strcmp(classname, "func_vip_safetyzone") == 0 ||
            strcmp(classname, "info_vip_safetyzone") == 0) {
            sb_append_n(sb, "  \"spawn_anchor\" \"1\"\n", 21);
        }
        sb_append_n(sb, "}\n", 2);
    }
}

/* Material group structures for batching world geometry into meshes */
typedef struct {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
} MeshVert;

typedef struct {
    char mat[64];
    int nonsolid;
    MeshVert* verts;
    unsigned int num_verts;
    unsigned int cap_verts;
    unsigned int* indices;
    unsigned int num_indices;
    unsigned int cap_indices;
} MeshGroup;

static int add_tri_to_group(MeshGroup* g, const MeshVert* v0, const MeshVert* v1, const MeshVert* v2) {
    if (g->num_verts + 3 > g->cap_verts) {
        unsigned int nc = g->cap_verts ? g->cap_verts * 2 : 256;
        while (nc < g->num_verts + 3) nc *= 2;
        MeshVert* nv = (MeshVert*)realloc(g->verts, nc * sizeof(MeshVert));
        if (!nv) return 0;
        g->verts = nv;
        g->cap_verts = nc;
    }
    if (g->num_indices + 3 > g->cap_indices) {
        unsigned int nc = g->cap_indices ? g->cap_indices * 2 : 256;
        while (nc < g->num_indices + 3) nc *= 2;
        unsigned int* ni = (unsigned int*)realloc(g->indices, nc * sizeof(unsigned int));
        if (!ni) return 0;
        g->indices = ni;
        g->cap_indices = nc;
    }
    unsigned int base = g->num_verts;
    g->verts[base + 0] = *v0;
    g->verts[base + 1] = *v1;
    g->verts[base + 2] = *v2;
    g->num_verts += 3;

    g->indices[g->num_indices + 0] = base + 0;
    g->indices[g->num_indices + 1] = base + 1;
    g->indices[g->num_indices + 2] = base + 2;
    g->num_indices += 3;
    return 1;
}

int cs16_bsp_to_hxmap(const unsigned char* bsp_data, size_t bsp_size,
                      const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size) {
    if (!bsp_data || bsp_size < sizeof(BspHeader) || !out_buf || !out_size) return 0;

    const BspHeader* hdr = (const BspHeader*)bsp_data;
    if (hdr->version != GOLDSRC_BSP_VERSION) return 0;

    unsigned int num_planes = 0, num_verts = 0, num_edges = 0, num_surfedges = 0;
    unsigned int num_texinfo = 0, num_faces = 0, num_models = 0;

    if (!lump_ok(hdr, bsp_size, LUMP_PLANES, sizeof(BspPlane), &num_planes) ||
        !lump_ok(hdr, bsp_size, LUMP_VERTEXES, sizeof(BspVertex), &num_verts) ||
        !lump_ok(hdr, bsp_size, LUMP_EDGES, sizeof(BspEdge), &num_edges) ||
        !lump_ok(hdr, bsp_size, LUMP_SURFEDGES, sizeof(int), &num_surfedges) ||
        !lump_ok(hdr, bsp_size, LUMP_TEXINFO, sizeof(BspTexInfo), &num_texinfo) ||
        !lump_ok(hdr, bsp_size, LUMP_FACES, sizeof(BspFace), &num_faces) ||
        !lump_ok(hdr, bsp_size, LUMP_MODELS, sizeof(BspModel), &num_models) ||
        !lump_ok(hdr, bsp_size, LUMP_ENTITIES, 1, NULL) ||
        !lump_ok(hdr, bsp_size, LUMP_TEXTURES, 1, NULL)) {
        return 0;
    }

    const BspPlane* planes = (const BspPlane*)(bsp_data + hdr->lumps[LUMP_PLANES].offset);
    const BspVertex* verts = (const BspVertex*)(bsp_data + hdr->lumps[LUMP_VERTEXES].offset);
    const BspEdge* edges = (const BspEdge*)(bsp_data + hdr->lumps[LUMP_EDGES].offset);
    const int* surfedges = (const int*)(bsp_data + hdr->lumps[LUMP_SURFEDGES].offset);
    const BspTexInfo* texinfos = (const BspTexInfo*)(bsp_data + hdr->lumps[LUMP_TEXINFO].offset);
    const BspFace* faces = (const BspFace*)(bsp_data + hdr->lumps[LUMP_FACES].offset);
    const BspModel* models = (const BspModel*)(bsp_data + hdr->lumps[LUMP_MODELS].offset);

    StrBuf sb;
    if (!sb_init(&sb, 2 * 1024 * 1024)) return 0;

    char clean_map[64] = "cs16_map";
    if (map_name && map_name[0]) {
        const char* slash = strrchr(map_name, '/');
        const char* base = slash ? slash + 1 : map_name;
        size_t i = 0;
        while (base[i] && base[i] != '.' && i + 1 < sizeof(clean_map)) {
            clean_map[i] = base[i];
            i++;
        }
        clean_map[i] = '\0';
    }
    sb_appendf(&sb, "map {\n  version 2\n  name %s\n}\n", clean_map);

    /* Parse skyname and emit environment_skybox */
    const char* ent_text = (const char*)(bsp_data + hdr->lumps[LUMP_ENTITIES].offset);
    size_t ent_len = (size_t)hdr->lumps[LUMP_ENTITIES].length;
    char skyname[64] = "des";
    parse_skyname_from_worldspawn(ent_text, ent_len, skyname, sizeof(skyname));

    sb_append_n(&sb, "entity {\n", 9);
    sb_append_n(&sb, "  \"classname\" \"environment_skybox\"\n", 35);
    sb_appendf(&sb, "  \"face_rt\" \"gfx/env/%srt.tga\"\n", skyname);
    sb_appendf(&sb, "  \"face_lf\" \"gfx/env/%slf.tga\"\n", skyname);
    sb_appendf(&sb, "  \"face_bk\" \"gfx/env/%sbk.tga\"\n", skyname);
    sb_appendf(&sb, "  \"face_ft\" \"gfx/env/%sft.tga\"\n", skyname);
    sb_appendf(&sb, "  \"face_up\" \"gfx/env/%sup.tga\"\n", skyname);
    sb_appendf(&sb, "  \"face_dn\" \"gfx/env/%sdn.tga\"\n", skyname);
    sb_append_n(&sb, "}\n", 2);

    /* Group visible faces into meshes by material */
    #define MAX_GROUPS 256
    MeshGroup groups[MAX_GROUPS];
    int group_count = 0;
    memset(groups, 0, sizeof(groups));

    /* Determine visible models from entities (submodels like crates, decorations, walls) */
    unsigned char* is_visible_model = (unsigned char*)calloc(num_models ? num_models : 1, 1);
    if (is_visible_model) {
        get_visible_models(ent_text, ent_len, is_visible_model, num_models);
    }

    MeshVert poly[64];
    for (unsigned int midx = 0; midx < (num_models ? num_models : 1); ++midx) {
        if (num_models > 0 && is_visible_model && !is_visible_model[midx]) continue;

        unsigned int start_face = (num_models > 0) ? (unsigned int)models[midx].firstface : 0;
        unsigned int face_count = (num_models > 0) ? (unsigned int)models[midx].numfaces : num_faces;
        unsigned int end_face = start_face + face_count;
        if (end_face > num_faces) end_face = num_faces;

        for (unsigned int fi = start_face; fi < end_face; ++fi) {
            const BspFace* f = &faces[fi];
            if (f->numedges < 3 || f->numedges > 64) continue;
            if (f->firstedge < 0 || (unsigned int)(f->firstedge + f->numedges) > num_surfedges) continue;
            if (f->planenum < 0 || (unsigned int)f->planenum >= num_planes) continue;

            char mat[64] = "common/caulk";
            unsigned int tex_w = 16, tex_h = 16;
            if (f->texinfo >= 0 && (unsigned int)f->texinfo < num_texinfo) {
                get_miptex_info(bsp_data, hdr, texinfos[f->texinfo].miptex, mat, sizeof(mat), &tex_w, &tex_h);
            }
            /* Skip sky, triggers, clips from solid world geometry */
            if (strstr(mat, "sky") != NULL || strstr(mat, "aaatrigger") != NULL || strstr(mat, "clip") != NULL) {
                continue;
            }

            const BspPlane* pl = &planes[f->planenum];
            float fn[3] = {
                f->side ? -pl->normal[0] : pl->normal[0],
                f->side ? -pl->normal[1] : pl->normal[1],
                f->side ? -pl->normal[2] : pl->normal[2]
            };

            const BspTexInfo* ti = &texinfos[f->texinfo];
            float inv_w = 1.0f / (float)tex_w;
            float inv_h = 1.0f / (float)tex_h;

            int npts = 0;
            for (int ei = 0; ei < f->numedges; ++ei) {
                int se = surfedges[f->firstedge + ei];
                unsigned int eidx = (unsigned int)(se >= 0 ? se : -se);
                if (eidx >= num_edges) { npts = 0; break; }
                unsigned short vidx = (se >= 0) ? edges[eidx].v[0] : edges[eidx].v[1];
                if (vidx >= num_verts) { npts = 0; break; }

                float px = verts[vidx].point[0];
                float py = verts[vidx].point[1];
                float pz = verts[vidx].point[2];

                float u = (px * ti->vecs[0][0] + py * ti->vecs[0][1] + pz * ti->vecs[0][2] + ti->vecs[0][3]) * inv_w;
                float v = (px * ti->vecs[1][0] + py * ti->vecs[1][1] + pz * ti->vecs[1][2] + ti->vecs[1][3]) * inv_h;

                poly[npts].x = px;
                poly[npts].y = py;
                poly[npts].z = pz;
                poly[npts].u = u;
                poly[npts].v = v;
                poly[npts].nx = fn[0];
                poly[npts].ny = fn[1];
                poly[npts].nz = fn[2];
                npts++;
            }
            if (npts < 3) continue;

            /* Find or create group */
            int is_nonsolid = (num_models > 0 && is_visible_model && is_visible_model[midx] == 2) ? 1 : 0;
            int gidx = -1;
            for (int g = 0; g < group_count; ++g) {
                if (groups[g].nonsolid == is_nonsolid && strcmp(groups[g].mat, mat) == 0) {
                    gidx = g;
                    break;
                }
            }
            if (gidx < 0) {
                if (group_count < MAX_GROUPS) {
                    gidx = group_count++;
                    snprintf(groups[gidx].mat, sizeof(groups[gidx].mat), "%s", mat);
                    groups[gidx].nonsolid = is_nonsolid;
                } else {
                    gidx = 0;
                }
            }

            /* GoldSrc BSP edges wind clockwise with respect to face normal; reverse order (0, i+1, i) so normal points outward */
            for (int i = 1; i + 1 < npts; ++i) {
                add_tri_to_group(&groups[gidx], &poly[0], &poly[i + 1], &poly[i]);
            }
        }
    }
    if (is_visible_model) free(is_visible_model);

    /* Emit meshes */
    for (int g = 0; g < group_count; ++g) {
        MeshGroup* grp = &groups[g];
        if (grp->num_indices == 0) continue;

        sb_append_n(&sb, "mesh {\n", 7);
        sb_appendf(&sb, "  material \"%s\"\n", grp->mat);
        if (grp->nonsolid) {
            sb_append_n(&sb, "  contents nonsolid\n", 20);
        } else {
            sb_append_n(&sb, "  contents solid\n", 17);
        }

        for (unsigned int v = 0; v < grp->num_verts; ++v) {
            const MeshVert* mv = &grp->verts[v];
            sb_appendf(&sb, "  v %.2f %.2f %.2f %.4f %.4f %.3f %.3f %.3f\n",
                       mv->x, mv->y, mv->z, mv->u, mv->v, mv->nx, mv->ny, mv->nz);
        }
        for (unsigned int idx = 0; idx + 2 < grp->num_indices; idx += 3) {
            sb_appendf(&sb, "  f %u %u %u\n", grp->indices[idx], grp->indices[idx + 1], grp->indices[idx + 2]);
        }
        sb_append_n(&sb, "}\n", 2);

        free(grp->verts);
        free(grp->indices);
    }

    /* Emit entities (spawns, items, breakables, ladders) */
    emit_entities(&sb, ent_text, ent_len, models, num_models);

    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
