#include "doom_map.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    short x;
    short y;
    short angle;
    short type;
    short options;
} DoomThing;

typedef struct {
    unsigned short v1;
    unsigned short v2;
    unsigned short flags;
    unsigned short special;
    unsigned short tag;
    unsigned short sidenum[2];
} DoomLinedef;

typedef struct {
    short xoffset;
    short yoffset;
    char toptexture[8];
    char bottomtexture[8];
    char midtexture[8];
    unsigned short sector;
} DoomSidedef;

typedef struct {
    short x;
    short y;
} DoomVertex;

typedef struct {
    short floorheight;
    short ceilingheight;
    char floorpic[8];
    char ceilingpic[8];
    short lightlevel;
    short special;
    short tag;
} DoomSector;
#pragma pack(pop)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_init(StrBuf* sb, size_t cap) {
    sb->cap = cap ? cap : 262144;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) return 0;
    sb->data[0] = '\0';
    return 1;
}

static int sb_append_n(StrBuf* sb, const char* s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t nc = sb->cap * 2;
        while (nc < sb->len + n + 1) nc *= 2;
        char* p = (char*)realloc(sb->data, nc);
        if (!p) return 0;
        sb->data = p;
        sb->cap = nc;
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
    if ((size_t)n < sizeof(tmp)) return sb_append_n(sb, tmp, (size_t)n);
    char* big = (char*)malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = sb_append_n(sb, big, (size_t)n);
    free(big);
    return ok;
}

static void clean_tex8(const char in[8], char* out, size_t cap) {
    char tmp[16];
    size_t j = 0;
    for (int i = 0; i < 8 && in[i] && j + 1 < sizeof(tmp); ++i) {
        char c = (char)tolower((unsigned char)in[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) c = '_';
        tmp[j++] = c;
    }
    tmp[j] = '\0';
    if (j == 0 || (j == 1 && tmp[0] == '_')) snprintf(out, cap, "common/caulk");
    else snprintf(out, cap, "textures/%s", tmp);
}

static void emit_aabb_brush(StrBuf* sb, float x0, float y0, float z0,
                            float x1, float y1, float z1, const char* mat) {
    if (x1 <= x0 + 0.5f || y1 <= y0 + 0.5f || z1 <= z0 + 0.5f) return;
    sb_appendf(sb,
        "  brush {\n"
        "    mins %.1f %.1f %.1f\n"
        "    maxs %.1f %.1f %.1f\n"
        "    material \"%s\"\n"
        "  }\n",
        x0, y0, z0, x1, y1, z1, mat);
}

static void emit_wall_slab(StrBuf* sb, float x1, float y1, float x2, float y2,
                           float z0, float z1, const char* mat) {
    if (z1 <= z0 + 0.5f) return;
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    float nx = (dy / len) * 2.0f;
    float ny = (-dx / len) * 2.0f;

    float ax1 = x1 - nx, ay1 = y1 - ny;
    float ax2 = x2 - nx, ay2 = y2 - ny;
    float bx1 = x1 + nx, by1 = y1 + ny;
    float bx2 = x2 + nx, by2 = y2 + ny;

    sb_append_n(sb, "  brush {\n", 10);
    /* Top */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               ax1, ay1, z1, ax2, ay2, z1, bx2, by2, z1, mat);
    /* Bottom */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               bx2, by2, z0, ax2, ay2, z0, ax1, ay1, z0, mat);
    /* Front */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               ax1, ay1, z0, ax2, ay2, z0, ax2, ay2, z1, mat);
    /* Back */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               bx2, by2, z0, bx1, by1, z0, bx1, by1, z1, mat);
    /* Left */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               bx1, by1, z0, ax1, ay1, z0, ax1, ay1, z1, mat);
    /* Right */
    sb_appendf(sb, "    ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) ( %.1f %.1f %.1f ) %s 0 0 0 1 1\n",
               ax2, ay2, z0, bx2, by2, z0, bx2, by2, z1, mat);
    sb_append_n(sb, "  }\n", 4);
}

int doom_map_to_hxmap(const DoomWad* w, const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size) {
    if (!w || !map_name || !out_buf || !out_size) return 0;

    int marker = doom_wad_find_lump(w, map_name, 0);
    if (marker < 0) return 0;

    int things_idx   = doom_wad_find_lump(w, "THINGS", marker + 1);
    int linedefs_idx = doom_wad_find_lump(w, "LINEDEFS", marker + 1);
    int sidedefs_idx = doom_wad_find_lump(w, "SIDEDEFS", marker + 1);
    int vertexes_idx = doom_wad_find_lump(w, "VERTEXES", marker + 1);
    int sectors_idx  = doom_wad_find_lump(w, "SECTORS", marker + 1);

    if (things_idx < 0 || linedefs_idx < 0 || sidedefs_idx < 0 ||
        vertexes_idx < 0 || sectors_idx < 0) {
        return 0;
    }

    unsigned int sz_things = 0, sz_lines = 0, sz_sides = 0, sz_verts = 0, sz_secs = 0;
    const DoomThing*   things   = (const DoomThing*)doom_wad_lump_ptr(w, things_idx, &sz_things);
    const DoomLinedef* linedefs = (const DoomLinedef*)doom_wad_lump_ptr(w, linedefs_idx, &sz_lines);
    const DoomSidedef* sidedefs = (const DoomSidedef*)doom_wad_lump_ptr(w, sidedefs_idx, &sz_sides);
    const DoomVertex*  verts    = (const DoomVertex*)doom_wad_lump_ptr(w, vertexes_idx, &sz_verts);
    const DoomSector*  sectors  = (const DoomSector*)doom_wad_lump_ptr(w, sectors_idx, &sz_secs);

    unsigned int n_things = sz_things / sizeof(DoomThing);
    unsigned int n_lines  = sz_lines / sizeof(DoomLinedef);
    unsigned int n_sides  = sz_sides / sizeof(DoomSidedef);
    unsigned int n_verts  = sz_verts / sizeof(DoomVertex);
    unsigned int n_secs   = sz_secs / sizeof(DoomSector);

    if (n_verts == 0 || n_secs == 0 || n_lines == 0) return 0;

    StrBuf sb;
    if (!sb_init(&sb, 512 * 1024)) return 0;

    char clean_map[64];
    size_t mi = 0;
    for (; map_name[mi] && mi + 1 < sizeof(clean_map); ++mi) {
        clean_map[mi] = (char)tolower((unsigned char)map_name[mi]);
    }
    clean_map[mi] = '\0';

    sb_appendf(&sb, "map {\n  version 2\n  name %s\n}\n", clean_map);

    /* Compute sector bounding boxes for floor/ceiling slabs */
    float* sec_minx = (float*)malloc(n_secs * sizeof(float));
    float* sec_miny = (float*)malloc(n_secs * sizeof(float));
    float* sec_maxx = (float*)malloc(n_secs * sizeof(float));
    float* sec_maxy = (float*)malloc(n_secs * sizeof(float));
    unsigned char* sec_used = (unsigned char*)calloc(n_secs, 1);

    for (unsigned int s = 0; s < n_secs; ++s) {
        sec_minx[s] = 1e9f; sec_miny[s] = 1e9f;
        sec_maxx[s] = -1e9f; sec_maxy[s] = -1e9f;
    }

    for (unsigned int i = 0; i < n_lines; ++i) {
        const DoomLinedef* ld = &linedefs[i];
        if (ld->v1 >= n_verts || ld->v2 >= n_verts) continue;
        float x1 = (float)verts[ld->v1].x, y1 = (float)verts[ld->v1].y;
        float x2 = (float)verts[ld->v2].x, y2 = (float)verts[ld->v2].y;

        for (int s = 0; s < 2; ++s) {
            if (ld->sidenum[s] == 0xFFFF || ld->sidenum[s] >= n_sides) continue;
            unsigned short sec = sidedefs[ld->sidenum[s]].sector;
            if (sec >= n_secs) continue;
            sec_used[sec] = 1;
            if (x1 < sec_minx[sec]) sec_minx[sec] = x1;
            if (x2 < sec_minx[sec]) sec_minx[sec] = x2;
            if (y1 < sec_miny[sec]) sec_miny[sec] = y1;
            if (y2 < sec_miny[sec]) sec_miny[sec] = y2;
            if (x1 > sec_maxx[sec]) sec_maxx[sec] = x1;
            if (x2 > sec_maxx[sec]) sec_maxx[sec] = x2;
            if (y1 > sec_maxy[sec]) sec_maxy[sec] = y1;
            if (y2 > sec_maxy[sec]) sec_maxy[sec] = y2;
        }

        /* Emit walls */
        if (ld->sidenum[0] != 0xFFFF && ld->sidenum[0] < n_sides) {
            const DoomSidedef* sd0 = &sidedefs[ld->sidenum[0]];
            if (sd0->sector < n_secs) {
                const DoomSector* sec0 = &sectors[sd0->sector];
                if (ld->sidenum[1] == 0xFFFF || ld->sidenum[1] >= n_sides) {
                    char mat[64];
                    clean_tex8(sd0->midtexture, mat, sizeof(mat));
                    emit_wall_slab(&sb, x1, y1, x2, y2,
                                   (float)sec0->floorheight, (float)sec0->ceilingheight, mat);
                } else {
                    const DoomSidedef* sd1 = &sidedefs[ld->sidenum[1]];
                    if (sd1->sector < n_secs) {
                        const DoomSector* sec1 = &sectors[sd1->sector];
                        float f0 = (float)sec0->floorheight, f1 = (float)sec1->floorheight;
                        float c0 = (float)sec0->ceilingheight, c1 = (float)sec1->ceilingheight;
                        if (f1 > f0) {
                            char mat[64];
                            clean_tex8(sd0->bottomtexture, mat, sizeof(mat));
                            emit_wall_slab(&sb, x1, y1, x2, y2, f0, f1, mat);
                        } else if (f0 > f1) {
                            char mat[64];
                            clean_tex8(sd1->bottomtexture, mat, sizeof(mat));
                            emit_wall_slab(&sb, x1, y1, x2, y2, f1, f0, mat);
                        }
                        if (c0 > c1) {
                            char mat[64];
                            clean_tex8(sd0->toptexture, mat, sizeof(mat));
                            emit_wall_slab(&sb, x1, y1, x2, y2, c1, c0, mat);
                        } else if (c1 > c0) {
                            char mat[64];
                            clean_tex8(sd1->toptexture, mat, sizeof(mat));
                            emit_wall_slab(&sb, x1, y1, x2, y2, c0, c1, mat);
                        }
                    }
                }
            }
        }
    }

    for (unsigned int s = 0; s < n_secs; ++s) {
        if (!sec_used[s]) continue;
        char fmat[64], cmat[64];
        clean_tex8(sectors[s].floorpic, fmat, sizeof(fmat));
        clean_tex8(sectors[s].ceilingpic, cmat, sizeof(cmat));
        float fz = (float)sectors[s].floorheight;
        float cz = (float)sectors[s].ceilingheight;
        emit_aabb_brush(&sb, sec_minx[s], sec_miny[s], fz - 16.0f,
                        sec_maxx[s], sec_maxy[s], fz, fmat);
        emit_aabb_brush(&sb, sec_minx[s], sec_miny[s], cz,
                        sec_maxx[s], sec_maxy[s], cz + 16.0f, cmat);
    }

    free(sec_minx); free(sec_miny); free(sec_maxx); free(sec_maxy); free(sec_used);

    /* Emit THINGS as entities */
    for (unsigned int i = 0; i < n_things; ++i) {
        const DoomThing* t = &things[i];
        const char* cls = NULL;
        if (t->type == 1) cls = "info_player_start";
        else if (t->type == 11) cls = "info_player_deathmatch";
        else if (t->type == 2011 || t->type == 2012) cls = "item_health";
        else if (t->type == 2018 || t->type == 2019) cls = "item_armor";
        else if (t->type >= 2001 && t->type <= 2006) cls = "weapon_shotgun";
        else if (t->type == 3004 || t->type == 9 || t->type == 3001 ||
                 t->type == 3002 || t->type == 3005 || t->type == 3003 || t->type == 16) {
            cls = "monster_doom";
        }
        if (!cls) continue;

        /* Find approximate floor Z from first sector containing (x,y) or 0 */
        float z = 8.0f;
        sb_appendf(&sb,
            "entity {\n"
            "  \"classname\" \"%s\"\n"
            "  \"origin\" \"%.1f %.1f %.1f\"\n"
            "  \"angle\" \"%d\"\n"
            "  \"doom_type\" \"%d\"\n"
            "}\n",
            cls, (float)t->x, (float)t->y, z, (int)t->angle, (int)t->type);
    }

    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
