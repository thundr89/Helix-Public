/*
 * CoD4 compiled map (IBSP version 22) to a Helix map.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The file is little-endian. After the 4-byte magic and the version,
 * a count says how many lumps are present. Each present lump is an id
 * and a byte length. Payloads are packed after that directory, each
 * padded up to a 4-byte boundary. Brushes and the entity text are the
 * map. A brush is a side count plus that many sides; the first six sides
 * are the axial bounds. Sky materials are not drawn. Draw triangles are
 * only a fallback when the file has no brush lump.
 */

#include "cod4_bsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    COD4_LUMP_MATERIALS = 0,
    COD4_LUMP_BRUSHSIDES = 5,
    COD4_LUMP_BRUSHES = 8,
    COD4_LUMP_FACES = 9,
    COD4_LUMP_VERTICES = 10,
    COD4_LUMP_INDICES = 11,
    COD4_LUMP_ENTITIES = 39,
    COD4_FACE_BYTES = 24,
    COD4_VERTEX_BYTES = 68,
    COD4_MATERIAL_BYTES = 72
};

typedef struct Buf {
    char* p;
    unsigned n;
    unsigned cap;
} Buf;

static unsigned rd_u16(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd_u32(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static float rd_f32(const unsigned char* p) {
    unsigned u = rd_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int finite_f(float v) { return v == v && v < 1.0e30f && v > -1.0e30f; }

/* A triangle index is local to the face. Byte 12 is that face's first vertex.
 * The draw list also contains a sky volume around the room. Its corners sit
 * past the ground footprint, and in the original game that volume is a sky
 * shader rather than a solid shell. */
static int same_coord(float a, float b, float c) {
    return fabsf(a - b) <= 1.f && fabsf(a - c) <= 1.f;
}

static int keep_draw_tri(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz) {
    float ux = bx - ax, uy = by - ay, uz = bz - az;
    float vx = cx - ax, vy = cy - ay, vz = cz - az;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    if (nx * nx + ny * ny + nz * nz < 1.0e-8f) return 0;
    return same_coord(ax, bx, cx) || same_coord(ay, by, cy) || same_coord(az, bz, cz);
}

static void room_extent(const unsigned char* data, unsigned vert_at, unsigned verts, float* room_xy, float* room_z) {
    unsigned i;
    float xy = 0.f;
    float ztop = 0.f;
    for (i = 0; i < verts; i++) {
        const unsigned char* v = data + vert_at + i * COD4_VERTEX_BYTES;
        float x = rd_f32(v), y = rd_f32(v + 4), z = rd_f32(v + 8);
        float ax = fabsf(x), ay = fabsf(y);
        if (!finite_f(x) || !finite_f(y) || !finite_f(z) || fabsf(z) > 64.f) continue;
        if (ax > xy) xy = ax;
        if (ay > xy) xy = ay;
    }
    for (i = 0; i < verts; i++) {
        const unsigned char* v = data + vert_at + i * COD4_VERTEX_BYTES;
        float x = rd_f32(v), y = rd_f32(v + 4), z = rd_f32(v + 8);
        if (!finite_f(x) || !finite_f(y) || !finite_f(z)) continue;
        if (fabsf(x) <= xy + 1.f && fabsf(y) <= xy + 1.f && z > ztop) ztop = z;
    }
    if (xy < 1.f) {
        xy = 1.0e9f;
        ztop = 1.0e9f;
    }
    *room_xy = xy;
    *room_z = ztop;
}

static int in_room(float x, float y, float z, float room_xy, float room_z) {
    if (!finite_f(x) || !finite_f(y) || !finite_f(z)) return 0;
    if (fabsf(x) > room_xy + 16.f || fabsf(y) > room_xy + 16.f) return 0;
    if (z > room_z + 32.f || z < -64.f) return 0;
    return 1;
}

/* Returns the three global vertex indices of face triangle t. */
static int face_tri(const unsigned char* data, unsigned index_at, unsigned indices, unsigned verts,
                    const unsigned char* face, unsigned t, unsigned* ia, unsigned* ib, unsigned* ic) {
    unsigned base = rd_u16(face + 12);
    unsigned count = rd_u16(face + 18);
    unsigned first = rd_u16(face + 20);
    unsigned a, b, c;
    if (count < 3 || t + 2 >= count || first > indices || count > indices - first) return 0;
    a = (unsigned)rd_u16(data + index_at + (first + t) * 2u) + base;
    b = (unsigned)rd_u16(data + index_at + (first + t + 1) * 2u) + base;
    c = (unsigned)rd_u16(data + index_at + (first + t + 2) * 2u) + base;
    if (a >= verts || b >= verts || c >= verts) return 0;
    *ia = a;
    *ib = b;
    *ic = c;
    return 1;
}

static int buf_add(Buf* b, const char* s, unsigned len) {
    if (b->n + len + 1 > b->cap) {
        unsigned cap = b->cap ? b->cap : 256;
        char* grown;
        while (cap < b->n + len + 1) {
            if (cap > 0x7fffffffu / 2) return 0;
            cap *= 2;
        }
        grown = (char*)realloc(b->p, cap);
        if (!grown) return 0;
        b->p = grown;
        b->cap = cap;
    }
    if (len) memcpy(b->p + b->n, s, len);
    b->n += len;
    b->p[b->n] = 0;
    return 1;
}

static int buf_str(Buf* b, const char* s) { return buf_add(b, s, (unsigned)strlen(s)); }

static int buf_num(Buf* b, float v) {
    char tmp[64];
    int n;
    if (!finite_f(v)) v = 0.f;
    n = snprintf(tmp, sizeof(tmp), "%.8g", (double)v);
    if (n <= 0 || n >= (int)sizeof(tmp)) return 0;
    return buf_add(b, tmp, (unsigned)n);
}

static const char* block_classname(const char* block, unsigned n, unsigned* len) {
    const char* key = "\"classname\"";
    const char* p = block;
    const char* end = block + n;
    const char* q1;
    const char* q2;
    unsigned kn = 11;
    while (p + kn < end) {
        if (memcmp(p, key, kn) == 0) break;
        p++;
    }
    if (p + kn >= end) return 0;
    p += kn;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p != '"') return 0;
    q1 = p + 1;
    q2 = q1;
    while (q2 < end && *q2 != '"') q2++;
    if (q2 >= end) return 0;
    if (len) *len = (unsigned)(q2 - q1);
    return q1;
}

/* These are the map's spawn entities. Helix selects them by the CoD 1 class
 * name; the original class is kept beside it. Nothing is chosen by geometry. */
static const char* spawn_anchor_name(const char* cls, unsigned n) {
    if (n == 11 && memcmp(cls, "mp_dm_spawn", 11) == 0) return "mp_deathmatch_spawn";
    if (n == 12 && memcmp(cls, "mp_tdm_spawn", 12) == 0) return "mp_teamdeathmatch_spawn";
    return 0;
}

static int block_is_player_spawn(const char* block, unsigned n) {
    const char* key = "\"classname\"";
    const char* p = block;
    const char* end = block + n;
    const char* q1;
    const char* q2;
    unsigned kn = 11;
    while (p + kn < end) {
        if (memcmp(p, key, kn) == 0) break;
        p++;
    }
    if (p + kn >= end) return 0;
    p += kn;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p != '"') return 0;
    q1 = p + 1;
    q2 = q1;
    while (q2 < end && *q2 != '"') q2++;
    if (q2 >= end) return 0;
    if ((unsigned)(q2 - q1) == 10 && memcmp(q1, "worldspawn", 10) == 0) return 0;
    while (q1 + 5 <= q2) {
        if (memcmp(q1, "spawn", 5) == 0 || (q1 + 6 <= q2 && memcmp(q1, "player", 6) == 0)) return 1;
        q1++;
    }
    return 0;
}

static int append_entities(Buf* b, const unsigned char* data, unsigned ent_at, unsigned ent_n) {
    unsigned i = 0;
    while (i < ent_n && (data[ent_at + i] == ' ' || data[ent_at + i] == '\t' || data[ent_at + i] == '\r' ||
                         data[ent_at + i] == '\n'))
        i++;
    if (i >= ent_n || data[ent_at + i] != '{') return 1;
    while (i < ent_n) {
        unsigned start, depth, k;
        char* block;
        unsigned bn = 0;
        if (data[ent_at + i] != '{') {
            char ch = (char)data[ent_at + i];
            if ((ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 32 && ch < 127)) && !buf_add(b, &ch, 1)) return 0;
            i++;
            continue;
        }
        start = i;
        depth = 0;
        for (; i < ent_n; i++) {
            unsigned char c = data[ent_at + i];
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    i++;
                    break;
                }
            }
        }
        block = (char*)malloc((size_t)(i - start) + 160u);
        if (!block) return 0;
        for (k = start; k < i; k++) {
            unsigned char c = data[ent_at + k];
            if (c == '\t' || c == '\n' || c == '\r' || (c >= 32 && c < 127)) block[bn++] = (char)c;
        }
        block[bn] = 0;
        {
            unsigned clen = 0;
            const char* cls = block_classname(block, bn, &clen);
            const char* anchor = cls ? spawn_anchor_name(cls, clen) : 0;
            unsigned cap = (unsigned)(i - start) + 160u;
            if (anchor && bn > 0 && block[bn - 1] == '}' && bn < cap) {
                int add = snprintf(block + bn - 1, (size_t)cap - (bn - 1),
                                   "\"spawn_anchor\" \"1\"\n\"cod_classname\" \"%s\"\n}", anchor);
                if (add < 0 || (unsigned)add >= cap - (bn - 1)) {
                    free(block);
                    return 0;
                }
                bn = (bn - 1) + (unsigned)add;
            }
        }
        if (block_is_player_spawn(block, bn)) {
            char* origin = strstr(block, "\"origin\"");
            if (origin) {
                char* q = strchr(origin + 8, '"');
                float x, y, z;
                if (q && sscanf(q + 1, "%f %f %f", &x, &y, &z) == 3) {
                    char* q2 = strchr(q + 1, '"');
                    char rebuilt[96];
                    int rn;
                    z += 24.f;
                    rn = snprintf(rebuilt, sizeof(rebuilt), "\"origin\" \"%.8g %.8g %.8g\"", (double)x, (double)y, (double)z);
                    if (rn > 0 && q2 && rn < (int)sizeof(rebuilt)) {
                        if (!buf_add(b, block, (unsigned)(origin - block)) || !buf_add(b, rebuilt, (unsigned)rn) ||
                            !buf_add(b, q2 + 1, (unsigned)(block + bn - (q2 + 1)))) {
                            free(block);
                            return 0;
                        }
                        free(block);
                        if (!buf_str(b, "\n")) return 0;
                        continue;
                    }
                }
            }
        }
        if (!buf_add(b, block, bn) || !buf_str(b, "\n")) {
            free(block);
            return 0;
        }
        free(block);
    }
    return 1;
}

static void map_token(const char* name, char* out, int cap) {
    const char* base = name ? name : "";
    const char* s;
    int n = 0;
    for (s = base; *s; s++)
        if (*s == '/' || *s == '\\') base = s + 1;
    for (s = base; *s && *s != '.' && n + 1 < cap; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') out[n++] = c;
    }
    if (!n && cap > 4) {
        memcpy(out, "cod4", 5);
        return;
    }
    if (cap) out[n] = 0;
}

static int material_has(const unsigned char* mat, const char* needle) {
    char name[65];
    unsigned i, n = 0;
    for (i = 0; i < 64 && mat[i]; i++) {
        char c = (char)mat[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        name[n++] = c;
    }
    name[n] = 0;
    return strstr(name, needle) != 0;
}

static int emit_box(Buf* b, float x0, float y0, float z0, float x1, float y1, float z1, int clip) {
    char line[1600];
    int n = snprintf(line, sizeof(line),
                     "brush {\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                     "%scontents solid\n}\n",
                     (double)x0, (double)y0, (double)z1, (double)x1, (double)y0, (double)z1, (double)x1, (double)y1, (double)z1,
                     (double)x0, (double)y1, (double)z0, (double)x1, (double)y1, (double)z0, (double)x1, (double)y0, (double)z0,
                     (double)x1, (double)y0, (double)z0, (double)x1, (double)y1, (double)z0, (double)x1, (double)y1, (double)z1,
                     (double)x0, (double)y1, (double)z0, (double)x0, (double)y0, (double)z0, (double)x0, (double)y0, (double)z1,
                     (double)x0, (double)y1, (double)z0, (double)x1, (double)y1, (double)z0, (double)x1, (double)y1, (double)z1,
                     (double)x1, (double)y0, (double)z0, (double)x0, (double)y0, (double)z0, (double)x0, (double)y0, (double)z1,
                     clip ? "visible false\n" : "");
    if (n <= 0 || n >= (int)sizeof(line)) return 0;
    return buf_add(b, line, (unsigned)n);
}

/* Returns the number of brushes written, or -1 when this file has no brush lump. */
static int emit_disk_brushes(Buf* b, const unsigned char* data, unsigned mat_at, unsigned mat_n, unsigned brush_at,
                             unsigned brush_n, unsigned side_at, unsigned side_n) {
    unsigned nbrush, nsides, i, need, si;
    int emitted = 0;
    if (!brush_n || !side_n || !mat_n) return -1;
    if (brush_n % 4u || side_n % 8u || mat_n % COD4_MATERIAL_BYTES) return -1;
    nbrush = brush_n / 4u;
    nsides = side_n / 8u;
    need = 0;
    for (i = 0; i < nbrush; i++) {
        unsigned ns = rd_u16(data + brush_at + i * 4u);
        if (ns < 6u) return -1;
        need += ns;
    }
    if (need != nsides) return -1;
    si = 0;
    for (i = 0; i < nbrush; i++) {
        unsigned ns = rd_u16(data + brush_at + i * 4u);
        unsigned mat = rd_u16(data + brush_at + i * 4u + 2u);
        const unsigned char* material;
        float mn[3], mx[3];
        int axis;
        if (mat >= mat_n / COD4_MATERIAL_BYTES) return -1;
        material = data + mat_at + mat * COD4_MATERIAL_BYTES;
        for (axis = 0; axis < 3; axis++) {
            mn[axis] = rd_f32(data + side_at + (si + (unsigned)axis * 2u) * 8u);
            mx[axis] = rd_f32(data + side_at + (si + (unsigned)axis * 2u + 1u) * 8u);
        }
        si += ns;
        if (material_has(material, "sky")) continue;
        if (!finite_f(mn[0]) || !finite_f(mn[1]) || !finite_f(mn[2]) || !finite_f(mx[0]) || !finite_f(mx[1]) ||
            !finite_f(mx[2]))
            continue;
        if (mx[0] <= mn[0] || mx[1] <= mn[1] || mx[2] <= mn[2]) continue;
        if (!emit_box(b, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], material_has(material, "clip"))) return -1;
        emitted++;
    }
    return emitted;
}

int cod4_bsp_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name,
                      unsigned char** out_buf, unsigned int* out_size) {
    unsigned num, i, cursor;
    unsigned face_at = 0, face_n = 0, vert_at = 0, vert_n = 0;
    unsigned index_at = 0, index_n = 0, ent_at = 0, ent_n = 0;
    unsigned mat_at = 0, mat_n = 0, brush_at = 0, brush_n = 0, side_at = 0, side_n = 0;
    unsigned verts, indices, faces, tri;
    int brush_count;
    float room_xy = 0.f, room_z = 0.f;
    char token[64];
    Buf buf;
    if (!data || !out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;
    if (size < 12 || memcmp(data, "IBSP", 4) != 0 || rd_u32(data + 4) != 22) return 0;
    num = rd_u32(data + 8);
    if (num == 0 || num > 64 || 12u + num * 8u > size) return 0;
    cursor = 12u + num * 8u;
    for (i = 0; i < num; i++) {
        unsigned id = rd_u32(data + 12u + i * 8u);
        unsigned len = rd_u32(data + 16u + i * 8u);
        if (len > size || cursor > size - len) return 0;
        if (id == COD4_LUMP_MATERIALS) {
            mat_at = cursor;
            mat_n = len;
        } else if (id == COD4_LUMP_BRUSHES) {
            brush_at = cursor;
            brush_n = len;
        } else if (id == COD4_LUMP_BRUSHSIDES) {
            side_at = cursor;
            side_n = len;
        } else if (id == COD4_LUMP_FACES) {
            face_at = cursor;
            face_n = len;
        } else if (id == COD4_LUMP_VERTICES) {
            vert_at = cursor;
            vert_n = len;
        } else if (id == COD4_LUMP_INDICES) {
            index_at = cursor;
            index_n = len;
        } else if (id == COD4_LUMP_ENTITIES) {
            ent_at = cursor;
            ent_n = len;
        }
        cursor += len;
        if (cursor & 3u) cursor = (cursor + 3u) & ~3u;
    }
    if (vert_n % COD4_VERTEX_BYTES || face_n % COD4_FACE_BYTES || (index_n & 1u)) return 0;
    verts = vert_n / COD4_VERTEX_BYTES;
    indices = index_n / 2u;
    faces = face_n / COD4_FACE_BYTES;
    if (verts > 1000000u || indices > 4000000u || faces > 1000000u) return 0;
    room_extent(data, vert_at, verts, &room_xy, &room_z);

    memset(&buf, 0, sizeof(buf));
    map_token(map_name, token, (int)sizeof(token));
    if (!buf_str(&buf, "map ") || !buf_str(&buf, token) || !buf_str(&buf, " {\nversion 2\n}\n")) {
        free(buf.p);
        return 0;
    }
    brush_count = emit_disk_brushes(&buf, data, mat_at, mat_n, brush_at, brush_n, side_at, side_n);
    if (brush_count < 0) {
        if (brush_n) {
            free(buf.p);
            return 0;
        }
        brush_count = 0;
    }
    tri = 0;
    if (brush_count > 0) goto entities;
    for (i = 0; i < faces; i++) {
        const unsigned char* f = data + face_at + i * COD4_FACE_BYTES;
        unsigned count = rd_u16(f + 18);
        unsigned t;
        for (t = 0; t + 2 < count; t += 3) {
            unsigned ia, ib, ic;
            const unsigned char *pa, *pb, *pc;
            float ax, ay, az, bx, by, bz, cx, cy, cz;
            float ux, uy, uz, vx, vy, vz, nx, ny, nz, len;
            char line[1200];
            int n;
            if (!face_tri(data, index_at, indices, verts, f, t, &ia, &ib, &ic)) continue;
            pa = data + vert_at + ia * COD4_VERTEX_BYTES;
            pb = data + vert_at + ib * COD4_VERTEX_BYTES;
            pc = data + vert_at + ic * COD4_VERTEX_BYTES;
            ax = rd_f32(pa); ay = rd_f32(pa + 4); az = rd_f32(pa + 8);
            bx = rd_f32(pb); by = rd_f32(pb + 4); bz = rd_f32(pb + 8);
            cx = rd_f32(pc); cy = rd_f32(pc + 4); cz = rd_f32(pc + 8);
            if (!keep_draw_tri(ax, ay, az, bx, by, bz, cx, cy, cz)) continue;
            if (!in_room(ax, ay, az, room_xy, room_z) || !in_room(bx, by, bz, room_xy, room_z) ||
                !in_room(cx, cy, cz, room_xy, room_z))
                continue;
            ux = bx - ax; uy = by - ay; uz = bz - az;
            vx = cx - ax; vy = cy - ay; vz = cz - az;
            nx = uy * vz - uz * vy;
            ny = uz * vx - ux * vz;
            nz = ux * vy - uy * vx;
            len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len < 1.0e-4f) continue;
            nx /= len; ny /= len; nz /= len;
            {
                float ax2 = ax - nx * 8.f, ay2 = ay - ny * 8.f, az2 = az - nz * 8.f;
                float bx2 = bx - nx * 8.f, by2 = by - ny * 8.f, bz2 = bz - nz * 8.f;
                float cx2 = cx - nx * 8.f, cy2 = cy - ny * 8.f, cz2 = cz - nz * 8.f;
                n = snprintf(line, sizeof(line),
                             "brush {\n"
                             "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                             "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                             "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                             "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                             "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                             "contents solid\n}\n",
                             (double)ax, (double)ay, (double)az, (double)bx, (double)by, (double)bz, (double)cx, (double)cy, (double)cz,
                             (double)ax2, (double)ay2, (double)az2, (double)cx2, (double)cy2, (double)cz2, (double)bx2, (double)by2, (double)bz2,
                             (double)ax, (double)ay, (double)az, (double)ax2, (double)ay2, (double)az2, (double)bx, (double)by, (double)bz,
                             (double)bx, (double)by, (double)bz, (double)bx2, (double)by2, (double)bz2, (double)cx, (double)cy, (double)cz,
                             (double)cx, (double)cy, (double)cz, (double)cx2, (double)cy2, (double)cz2, (double)ax, (double)ay, (double)az);
            }
            if (n <= 0 || n >= (int)sizeof(line) || !buf_add(&buf, line, (unsigned)n)) {
                free(buf.p);
                return 0;
            }
            tri++;
        }
    }
    /* Brushes own collision. The draw mesh stays nonsolid so its thick hulls do not fill the room. */
    if (!buf_str(&buf, "mesh {\n  material \"textures/dev/grid\"\n  contents nonsolid\n")) {
        free(buf.p);
        return 0;
    }
    for (i = 0; i < verts; i++) {
        const unsigned char* v = data + vert_at + i * COD4_VERTEX_BYTES;
        float x = rd_f32(v), y = rd_f32(v + 4), z = rd_f32(v + 8);
        float nx = rd_f32(v + 12), ny = rd_f32(v + 16), nz = rd_f32(v + 20);
        float u = rd_f32(v + 28), tv = rd_f32(v + 32);
        if (nx * nx + ny * ny + nz * nz < 1.0e-8f) {
            nx = 0.f;
            ny = 0.f;
            nz = 1.f;
        }
        if (!buf_str(&buf, "  v ") || !buf_num(&buf, x) || !buf_str(&buf, " ") || !buf_num(&buf, y) || !buf_str(&buf, " ") ||
            !buf_num(&buf, z) || !buf_str(&buf, " ") || !buf_num(&buf, u) || !buf_str(&buf, " ") || !buf_num(&buf, tv) ||
            !buf_str(&buf, " ") || !buf_num(&buf, nx) || !buf_str(&buf, " ") || !buf_num(&buf, ny) || !buf_str(&buf, " ") ||
            !buf_num(&buf, nz) || !buf_str(&buf, "\n")) {
            free(buf.p);
            return 0;
        }
    }
    for (i = 0; i < faces; i++) {
        const unsigned char* f = data + face_at + i * COD4_FACE_BYTES;
        unsigned count = rd_u16(f + 18);
        unsigned t;
        for (t = 0; t + 2 < count; t += 3) {
            unsigned a, b, c;
            char line[64];
            int n;
            if (!face_tri(data, index_at, indices, verts, f, t, &a, &b, &c)) continue;
            {
                const unsigned char* pa = data + vert_at + a * COD4_VERTEX_BYTES;
                const unsigned char* pb = data + vert_at + b * COD4_VERTEX_BYTES;
                const unsigned char* pc = data + vert_at + c * COD4_VERTEX_BYTES;
                float ax = rd_f32(pa), ay = rd_f32(pa + 4), az = rd_f32(pa + 8);
                float bx = rd_f32(pb), by = rd_f32(pb + 4), bz = rd_f32(pb + 8);
                float cx = rd_f32(pc), cy = rd_f32(pc + 4), cz = rd_f32(pc + 8);
                if (!keep_draw_tri(ax, ay, az, bx, by, bz, cx, cy, cz)) continue;
                if (!in_room(ax, ay, az, room_xy, room_z) || !in_room(bx, by, bz, room_xy, room_z) ||
                    !in_room(cx, cy, cz, room_xy, room_z))
                    continue;
            }
            n = snprintf(line, sizeof(line), "  f %u %u %u\n", a, b, c);
            if (n <= 0 || n >= (int)sizeof(line) || !buf_add(&buf, line, (unsigned)n)) {
                free(buf.p);
                return 0;
            }
        }
    }
    if (!buf_str(&buf, "}\n")) {
        free(buf.p);
        return 0;
    }
entities:
    if (brush_count <= 0 && tri == 0 && ent_n == 0) {
        free(buf.p);
        return 0;
    }
    if (ent_n && !append_entities(&buf, data, ent_at, ent_n)) {
        free(buf.p);
        return 0;
    }
    *out_buf = (unsigned char*)buf.p;
    *out_size = buf.n;
    return 1;
}
