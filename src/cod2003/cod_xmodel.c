/*
 * Call of Duty 1 (2003) xmodel -> OBJ
 * Copyright (C) 2026 Helix-Public contributors
 *
 * Binary layout (PK3, little endian).
 *
 *   xmodel version 14 (retail CoD1):
 *     u16 version
 *     f32 mins[3], maxs[3]
 *     3 times: f32 distance, NUL-terminated LOD surf name (empty = unused)
 *
 *   xmodel version 5 and 20:
 *     u16 version
 *     4 times: u32 pad, NUL-terminated LOD name (empty string = unused LOD)
 *
 *   xmodelsurfs:
 *     u16 version
 *     u16 numMeshes
 *     each mesh:
 *       u8  flags
 *       u16 unk
 *       u16 numVerts
 *       u16 numTris
 *       u16 numVerts2          (differs from numVerts when the mesh is skinned)
 *       if skinned: u16, then if numVerts2 != 0 a run of u16 until 0, then u16
 *       else:       u32 rigid bone
 *       each vert:
 *         f32 normal[3]
 *         u8  color[4]
 *         f32 uv[2]
 *         u8  pad[24]
 *         if skinned: u8 numWeights, u16 finalBone
 *         f32 pos[3]
 *         if skinned: numWeights times (u16 bone, u16 weight)
 *       each tri: u16 i0, i1, i2     (rewound to i0, i2, i1 for Helix)
 *
 *   xmodelsurfs version 14 (retail CoD1), per mesh:
 *     u8 skip, u16 numVerts, u16 numTris, u16 pad, u16 bone
 *     bone == 65535: rigged, then 4 pad bytes; weights are skipped (bind pose)
 *     triangle strips: u8 count, then u16 indices
 *     each vert: f32 normal[3], f32 uv[2], (rigged: u16 weights, u16 bone), f32 pos[3]
 *
 * ASCII XMODEL_EXPORT version 5 (Maya/Max tool output) is also accepted.
 */
#include "cod_xmodel.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Xsb;

static int xsb_init(Xsb* sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) return 0;
    sb->data[0] = '\0';
    return 1;
}

static int xsb_append(Xsb* sb, const char* str) {
    size_t n;
    if (!sb || !str) return 0;
    n = strlen(str);
    if (sb->len + n + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 4096;
        char* nd;
        while (sb->len + n + 1 > ncap) ncap *= 2;
        nd = (char*)realloc(sb->data, ncap);
        if (!nd) return 0;
        sb->data = nd;
        sb->cap = ncap;
    }
    memcpy(sb->data + sb->len, str, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

static int xsb_append_f(Xsb* sb, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return xsb_append(sb, buf);
}

static int need(size_t off, size_t n, size_t size) {
    return off <= size && n <= size - off;
}

static int ru16(const unsigned char* p, size_t size, size_t* off, unsigned short* o) {
    if (!need(*off, 2, size)) return 0;
    *o = (unsigned short)(p[*off] | (p[*off + 1] << 8));
    *off += 2;
    return 1;
}

static int ru32(const unsigned char* p, size_t size, size_t* off, unsigned int* o) {
    if (!need(*off, 4, size)) return 0;
    *o = (unsigned int)p[*off] | ((unsigned int)p[*off + 1] << 8) |
         ((unsigned int)p[*off + 2] << 16) | ((unsigned int)p[*off + 3] << 24);
    *off += 4;
    return 1;
}

static int rf32(const unsigned char* p, size_t size, size_t* off, float* o) {
    unsigned int u;
    if (!ru32(p, size, off, &u)) return 0;
    memcpy(o, &u, 4);
    return 1;
}

static int rcstr(const unsigned char* p, size_t size, size_t* off, char* out, size_t cap) {
    size_t n = 0;
    if (!out || cap == 0) return 0;
    while (*off < size && p[*off] != 0) {
        if (n + 1 < cap) out[n++] = (char)p[*off];
        *off += 1;
    }
    if (*off >= size) return 0;
    *off += 1; /* NUL */
    out[n] = '\0';
    return 1;
}

int cod_xmodel_is_text(const void* data, size_t size) {
    const char* p = (const char*)data;
    size_t i = 0;
    if (!data || size < 5) return 0;
    while (i < size && isspace((unsigned char)p[i])) i++;
    if (i + 5 <= size && memcmp(p + i, "MODEL", 5) == 0) return 1;
    if (i + 2 <= size && p[i] == '/' && p[i + 1] == '/') return 1;
    return 0;
}

int cod_xmodel_lod_name(const void* data, size_t size, char* out, size_t out_cap) {
    const unsigned char* p = (const unsigned char*)data;
    size_t off = 0;
    unsigned short ver = 0;
    int i;
    char name[128];
    if (!out || out_cap == 0) return 0;
    out[0] = '\0';
    if (!data || cod_xmodel_is_text(data, size)) return 0;
    if (!ru16(p, size, &off, &ver)) return 0;
    if (ver == 14) {
        /* mins/maxs, then up to three (distance, surf name) LOD slots. */
        if (!need(off, 24, size)) return 0;
        off += 24;
        for (i = 0; i < 3; i++) {
            float dist = 0.f;
            if (!rf32(p, size, &off, &dist)) return out[0] != '\0';
            (void)dist;
            if (!rcstr(p, size, &off, name, sizeof(name))) return out[0] != '\0';
            if (out[0] == '\0' && name[0]) {
                strncpy(out, name, out_cap - 1);
                out[out_cap - 1] = '\0';
            }
        }
        return out[0] != '\0';
    }
    if (ver != 5 && ver != 20) return 0;
    for (i = 0; i < 4; i++) {
        unsigned int pad = 0;
        if (!ru32(p, size, &off, &pad)) return 0;
        (void)pad;
        if (!rcstr(p, size, &off, name, sizeof(name))) return 0;
        if (out[0] == '\0' && name[0]) {
            strncpy(out, name, out_cap - 1);
            out[out_cap - 1] = '\0';
        }
    }
    return out[0] != '\0';
}

/* Retail CoD1 surfs. Rigid verts are already in model space. */
static int emit_surfs_v14(const unsigned char* p, size_t size, size_t off, Xsb* sb) {
    unsigned short num_meshes = 0;
    unsigned short m;
    int vert_base = 1;

    if (!ru16(p, size, &off, &num_meshes)) return 0;
    if (num_meshes == 0 || num_meshes > 64) return 0;

    for (m = 0; m < num_meshes; m++) {
        unsigned short num_verts = 0, num_tris = 0, pad = 0, bone = 0;
        unsigned short* tris = NULL;
        unsigned short* wcs = NULL;
        int ntri = 0;
        int stored = 0;
        int rigged;
        int guard = 0;
        unsigned short v;
        int ok = 1;

        if (!need(off, 1, size)) return 0;
        off++; /* unknown */
        if (!ru16(p, size, &off, &num_verts) || !ru16(p, size, &off, &num_tris)) return 0;
        if (!ru16(p, size, &off, &pad) || !ru16(p, size, &off, &bone)) return 0;
        (void)pad;
        if (num_verts == 0 || num_tris == 0 || num_verts > 65535) return 0;
        rigged = (bone == 65535);
        if (rigged) {
            if (!need(off, 4, size)) return 0;
            off += 4;
        }

        tris = (unsigned short*)malloc((size_t)num_tris * 3 * sizeof(unsigned short));
        wcs = (unsigned short*)calloc(num_verts, sizeof(unsigned short));
        if (!tris || !wcs) {
            free(tris);
            free(wcs);
            return 0;
        }

        while (ok && ntri < (int)num_tris) {
            unsigned char idx_count;
            unsigned short i1 = 0, i2 = 0, i3 = 0;
            int i;
            if (++guard > (int)num_tris * 4) ok = 0;
            if (!ok || !need(off, 1, size)) {
                ok = 0;
                break;
            }
            idx_count = p[off++];
            if (idx_count < 3) {
                ok = 0;
                break;
            }
            if (!ru16(p, size, &off, &i1) || !ru16(p, size, &off, &i2) || !ru16(p, size, &off, &i3)) {
                ok = 0;
                break;
            }
            if (i1 != i2 && i1 != i3 && i2 != i3) {
                if (i1 >= num_verts || i2 >= num_verts || i3 >= num_verts) {
                    ok = 0;
                    break;
                }
                if (stored < (int)num_tris) {
                    /* Flip CoD winding: (i1, i2, i3) -> (i1, i3, i2). */
                    tris[stored * 3 + 0] = i1;
                    tris[stored * 3 + 1] = i3;
                    tris[stored * 3 + 2] = i2;
                    stored++;
                }
                ntri++;
            }
            i = 3;
            /* Finish the strip even after num_tris is reached, or the next
             * bytes (vertex floats) are read as indices. */
            while (ok && i < (int)idx_count) {
                unsigned short i4 = i3;
                unsigned short i5 = 0;
                if (!ru16(p, size, &off, &i5)) {
                    ok = 0;
                    break;
                }
                if (i4 != i2 && i4 != i5 && i2 != i5) {
                    if (i4 >= num_verts || i2 >= num_verts || i5 >= num_verts) {
                        ok = 0;
                        break;
                    }
                    if (stored < (int)num_tris) {
                        tris[stored * 3 + 0] = i5;
                        tris[stored * 3 + 1] = i2;
                        tris[stored * 3 + 2] = i4;
                        stored++;
                    }
                    ntri++;
                }
                i++;
                if (!ok || i >= (int)idx_count) break;
                i2 = i5;
                if (!ru16(p, size, &off, &i3)) {
                    ok = 0;
                    break;
                }
                if (i4 != i2 && i4 != i3 && i2 != i3) {
                    if (i4 >= num_verts || i2 >= num_verts || i3 >= num_verts) {
                        ok = 0;
                        break;
                    }
                    if (stored < (int)num_tris) {
                        tris[stored * 3 + 0] = i3;
                        tris[stored * 3 + 1] = i2;
                        tris[stored * 3 + 2] = i4;
                        stored++;
                    }
                    ntri++;
                }
                i++;
            }
        }
        if (!ok || stored != (int)num_tris) {
            free(tris);
            free(wcs);
            return 0;
        }

        for (v = 0; ok && v < num_verts; v++) {
            float n[3], uv[2], pos[3];
            unsigned short wc = 0, vb = 0;
            if (!rf32(p, size, &off, &n[0]) || !rf32(p, size, &off, &n[1]) || !rf32(p, size, &off, &n[2]))
                ok = 0;
            if (!ok || !rf32(p, size, &off, &uv[0]) || !rf32(p, size, &off, &uv[1])) ok = 0;
            if (ok && rigged) {
                if (!ru16(p, size, &off, &wc) || !ru16(p, size, &off, &vb)) ok = 0;
                if (wc > 16) ok = 0;
                (void)vb;
                wcs[v] = wc;
            }
            if (!ok || !rf32(p, size, &off, &pos[0]) || !rf32(p, size, &off, &pos[1]) ||
                !rf32(p, size, &off, &pos[2]))
                ok = 0;
            if (ok && rigged && wc) {
                if (!need(off, 4, size)) ok = 0;
                else off += 4;
            }
            if (ok && !xsb_append_f(sb, "v %.6g %.6g %.6g\n", pos[0], pos[1], pos[2])) ok = 0;
            if (ok && !xsb_append_f(sb, "vt %.6g %.6g\n", uv[0], uv[1])) ok = 0;
            if (ok && !xsb_append_f(sb, "vn %.6g %.6g %.6g\n", n[0], n[1], n[2])) ok = 0;
        }
        if (ok && rigged) {
            for (v = 0; ok && v < num_verts; v++) {
                unsigned short w;
                for (w = 0; ok && w < wcs[v]; w++) {
                    if (!need(off, 18, size)) ok = 0;
                    else off += 18;
                }
            }
        }
        if (!ok) {
            free(tris);
            free(wcs);
            return 0;
        }
        {
            int t;
            for (t = 0; t < stored; t++) {
                int a = vert_base + (int)tris[t * 3 + 0];
                int b = vert_base + (int)tris[t * 3 + 1];
                int c = vert_base + (int)tris[t * 3 + 2];
                if (!xsb_append_f(sb, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, b, b, b, c, c, c)) {
                    free(tris);
                    free(wcs);
                    return 0;
                }
            }
        }
        vert_base += (int)num_verts;
        free(tris);
        free(wcs);
    }
    return 1;
}

static int emit_surfs(const unsigned char* p, size_t size, Xsb* sb) {
    size_t off = 0;
    unsigned short ver = 0, num_meshes = 0;
    unsigned short m;
    int vert_base = 1; /* OBJ is 1-based across meshes */

    if (!ru16(p, size, &off, &ver)) return 0;
    if (ver == 14) return emit_surfs_v14(p, size, off, sb);
    if (ver != 5 && ver != 20 && ver != 25) return 0;
    if (!ru16(p, size, &off, &num_meshes)) return 0;
    if (num_meshes == 0 || num_meshes > 256) return 0;

    for (m = 0; m < num_meshes; m++) {
        unsigned char flags = 0;
        unsigned short unk = 0, num_verts = 0, num_tris = 0, num_verts2 = 0;
        int skinned;
        unsigned short v, t;

        if (!need(off, 1, size)) return 0;
        flags = p[off++];
        (void)flags;
        if (!ru16(p, size, &off, &unk)) return 0;
        (void)unk;
        if (!ru16(p, size, &off, &num_verts)) return 0;
        if (!ru16(p, size, &off, &num_tris)) return 0;
        if (!ru16(p, size, &off, &num_verts2)) return 0;
        if (num_verts == 0 || num_verts > 65535 || num_tris == 0 || num_tris > 65535) return 0;

        skinned = num_verts != num_verts2;
        if (skinned) {
            unsigned short w = 0;
            int guard = 0;
            if (!ru16(p, size, &off, &w)) return 0;
            (void)w;
            if (num_verts2 != 0) {
                do {
                    if (!ru16(p, size, &off, &w)) return 0;
                    if (++guard > 100000) return 0;
                } while (w != 0);
                if (!ru16(p, size, &off, &w)) return 0;
            } else {
                unsigned int bone = 0;
                if (!ru32(p, size, &off, &bone)) return 0;
            }
        } else {
            unsigned int bone = 0;
            if (!ru32(p, size, &off, &bone)) return 0;
        }

        for (v = 0; v < num_verts; v++) {
            float n[3], uv[2], pos[3];
            unsigned char nw = 0;
            if (!rf32(p, size, &off, &n[0]) || !rf32(p, size, &off, &n[1]) || !rf32(p, size, &off, &n[2]))
                return 0;
            if (!need(off, 4, size)) return 0;
            off += 4; /* color */
            if (!rf32(p, size, &off, &uv[0]) || !rf32(p, size, &off, &uv[1])) return 0;
            if (!need(off, 24, size)) return 0;
            off += 24;
            if (skinned) {
                unsigned short final_bone = 0;
                if (!need(off, 1, size)) return 0;
                nw = p[off++];
                if (nw > 16) return 0;
                if (!ru16(p, size, &off, &final_bone)) return 0;
                (void)final_bone;
            }
            if (!rf32(p, size, &off, &pos[0]) || !rf32(p, size, &off, &pos[1]) || !rf32(p, size, &off, &pos[2]))
                return 0;
            if (skinned) {
                if (!need(off, (size_t)nw * 4, size)) return 0;
                off += (size_t)nw * 4;
            }
            if (!xsb_append_f(sb, "v %.6g %.6g %.6g\n", pos[0], pos[1], pos[2])) return 0;
            if (!xsb_append_f(sb, "vt %.6g %.6g\n", uv[0], uv[1])) return 0;
            if (!xsb_append_f(sb, "vn %.6g %.6g %.6g\n", n[0], n[1], n[2])) return 0;
        }

        for (t = 0; t < num_tris; t++) {
            unsigned short i0 = 0, i1 = 0, i2 = 0;
            int a, b, c;
            if (!ru16(p, size, &off, &i0) || !ru16(p, size, &off, &i1) || !ru16(p, size, &off, &i2)) return 0;
            if (i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) return 0;
            /* CoD winding is opposite Helix; same rewind as the BSP soup export. */
            a = vert_base + (int)i0;
            b = vert_base + (int)i2;
            c = vert_base + (int)i1;
            if (!xsb_append_f(sb, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, b, b, b, c, c, c)) return 0;
        }
        vert_base += (int)num_verts;
    }
    return 1;
}

static const char* skip_ws(const char* p, const char* end) {
    while (p < end && isspace((unsigned char)*p)) p++;
    return p;
}

static const char* next_line(const char* p, const char* end, char* buf, size_t cap) {
    size_t n = 0;
    if (p >= end) return NULL;
    while (p < end && *p != '\n' && *p != '\r') {
        if (n + 1 < cap) buf[n++] = *p;
        p++;
    }
    if (p < end && *p == '\r') p++;
    if (p < end && *p == '\n') p++;
    buf[n] = '\0';
    return p;
}

static int vmap_put(int** map, int* n, int index, int obj) {
    int i, nn;
    int* grown;
    if (index < 0 || index > 200000) return 0;
    if (index < *n) {
        (*map)[index] = obj;
        return 1;
    }
    nn = index + 1;
    grown = (int*)realloc(*map, (size_t)nn * sizeof(int));
    if (!grown) return 0;
    for (i = *n; i < nn; i++) grown[i] = 0;
    grown[index] = obj;
    *map = grown;
    *n = nn;
    return 1;
}

static int vmap_get(const int* map, int n, int index) {
    if (!map || index < 0 || index >= n) return 0;
    return map[index];
}

static int flush_vert(Xsb* sb, int** map, int* map_n, int vert_index, int obj_index,
                      float ox, float oy, float oz, float uu, float vv) {
    if (!xsb_append_f(sb, "v %.6g %.6g %.6g\nvt %.6g %.6g\nvn 0 0 1\n", ox, oy, oz, uu, vv)) return 0;
    return vmap_put(map, map_n, vert_index, obj_index);
}

static int emit_text(const char* text, size_t size, Xsb* sb) {
    const char* p = text;
    const char* end = text + size;
    int saw_version = 0;
    int have_vert = 0;
    int vert_index = 0;
    int obj_index = 1;
    float ox = 0, oy = 0, oz = 0, uu = 0, vv = 0;
    int* vmap = NULL;
    int vmap_n = 0;
    char line[512];
    int ok = 1;

    if (!xsb_append(sb, "# helix xmodel\n")) return 0;

    while (ok && (p = next_line(p, end, line, sizeof(line))) != NULL) {
        const char* s = skip_ws(line, line + strlen(line));
        float x, y, z, u, v;
        int ia, ib, ic, id, ie;
        if (s[0] == '\0' || s[0] == '#') continue;
        if (strncmp(s, "VERSION", 7) == 0) {
            int ver = 0;
            sscanf(s + 7, "%d", &ver);
            if (ver != 5 && ver != 6) ok = 0;
            else saw_version = 1;
            continue;
        }
        if (!ok) break;
        if (strncmp(s, "VERT", 4) == 0 && isspace((unsigned char)s[4])) {
            if (have_vert) {
                if (!flush_vert(sb, &vmap, &vmap_n, vert_index, obj_index, ox, oy, oz, uu, vv)) ok = 0;
                obj_index++;
            }
            have_vert = 1;
            ox = oy = oz = uu = vv = 0;
            if (sscanf(s + 4, "%d %f %f %f %f %f", &vert_index, &x, &y, &z, &u, &v) == 6) {
                ox = x;
                oy = y;
                oz = z;
                uu = u;
                vv = v;
            } else {
                sscanf(s + 4, "%d", &vert_index);
            }
            continue;
        }
        if (have_vert && strncmp(s, "OFFSET", 6) == 0) {
            if (sscanf(s + 6, "%f %f %f", &x, &y, &z) == 3) {
                ox = x;
                oy = y;
                oz = z;
            }
            continue;
        }
        if (have_vert && strncmp(s, "UV", 2) == 0 && isspace((unsigned char)s[2])) {
            /* "UV 1 u v" (channel, uv) or "UV u v". */
            if (sscanf(s + 2, "%d %f %f", &ia, &u, &v) == 3) {
                uu = u;
                vv = v;
            } else if (sscanf(s + 2, "%f %f", &u, &v) == 2) {
                uu = u;
                vv = v;
            }
            continue;
        }
        if (strncmp(s, "TRI", 3) == 0 && isspace((unsigned char)s[3])) {
            int fa, fb, fc;
            if (have_vert) {
                if (!flush_vert(sb, &vmap, &vmap_n, vert_index, obj_index, ox, oy, oz, uu, vv)) ok = 0;
                have_vert = 0;
                obj_index++;
            }
            /* TRI face material i0 i1 i2 — indices are the VERT numbers. */
            if (ok && sscanf(s + 3, "%d %d %d %d %d", &ia, &ib, &ic, &id, &ie) == 5) {
                fa = vmap_get(vmap, vmap_n, ic);
                fb = vmap_get(vmap, vmap_n, id);
                fc = vmap_get(vmap, vmap_n, ie);
                if (fa && fb && fc) {
                    if (!xsb_append_f(sb, "f %d %d %d\n", fa, fb, fc)) ok = 0;
                }
            }
            continue;
        }
    }
    if (ok && have_vert) {
        if (!flush_vert(sb, &vmap, &vmap_n, vert_index, obj_index, ox, oy, oz, uu, vv)) ok = 0;
    }
    free(vmap);
    return ok && saw_version && sb->len > 16;
}

int cod_xmodel_to_obj(const void* xmodel, size_t xmodel_size,
                      const void* surfs, size_t surfs_size,
                      unsigned char** out_buf, unsigned int* out_size) {
    Xsb sb;
    int ok = 0;
    if (!out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;
    if (!xsb_init(&sb)) return 0;

    if (xmodel && cod_xmodel_is_text(xmodel, xmodel_size)) {
        ok = emit_text((const char*)xmodel, xmodel_size, &sb);
    } else if (surfs && surfs_size >= 4) {
        if (!xsb_append(&sb, "# helix xmodel\n")) ok = 0;
        else ok = emit_surfs((const unsigned char*)surfs, surfs_size, &sb);
        if (xmodel && xmodel_size >= 2 && !cod_xmodel_is_text(xmodel, xmodel_size)) {
            unsigned short ver = 0;
            size_t off = 0;
            if (!ru16((const unsigned char*)xmodel, xmodel_size, &off, &ver) ||
                (ver != 5 && ver != 14 && ver != 20)) {
                ok = 0;
            }
        }
    }

    if (!ok || sb.len < 8) {
        free(sb.data);
        return 0;
    }
    *out_buf = (unsigned char*)sb.data;
    *out_size = (unsigned int)sb.len;
    return 1;
}
