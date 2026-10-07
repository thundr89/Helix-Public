/*
 * Call of Duty 1 (2003) xmodel -> OBJ
 * Copyright (C) 2026 Helix-Public contributors
 *
 * Lives in the modular hxfs_cod plugin (src/cod). src/cod2003 is a separate plugin and is left unchanged.
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
#include "xmodel/read.h"
#include "cod_util.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Xsb;

static char g_mats[64][160];
static int g_nmat = 0;
static int g_keep_hands = 0;
static CodXmodelBoneRule g_bone_rule = {1, 0};

void cod_xmodel_set_keep_hands(int keep) { g_keep_hands = keep ? 1 : 0; }

void cod_xmodel_set_bone_rule(CodXmodelBoneRule rule) { g_bone_rule = rule; }

CodXmodelBoneRule cod_xmodel_bone_rule(void) { return g_bone_rule; }

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

int cod_xmodel_lod_slots(const void* data, size_t size, CodXmodelLod* out, int cap) {
    const unsigned char* p = (const unsigned char*)data;
    size_t off = 0;
    unsigned short ver = 0;
    int i;
    char name[128];
    if (!data || cod_xmodel_is_text(data, size)) return 0;
    if (!ru16(p, size, &off, &ver)) return 0;
    if (ver == 14) {
        if (!need(off, 24, size)) return 0;
        off += 24;
        for (i = 0; i < 3; i++) {
            float dist = 0.f;
            if (!rf32(p, size, &off, &dist)) return i;
            if (!rcstr(p, size, &off, name, sizeof(name))) return i;
            if (out && i < cap) {
                out[i].distance = dist;
                strncpy(out[i].name, name, 63);
                out[i].name[63] = '\0';
            }
        }
        return 3;
    }
    if (ver != 5 && ver != 20) return 0;
    for (i = 0; i < 4; i++) {
        unsigned int pad = 0;
        if (!ru32(p, size, &off, &pad)) return i;
        (void)pad;
        if (!rcstr(p, size, &off, name, sizeof(name))) return i;
        if (out && i < cap) {
            out[i].distance = 0.f;
            strncpy(out[i].name, name, 63);
            out[i].name[63] = '\0';
        }
    }
    return 4;
}

int cod_xmodel_lod_name(const void* data, size_t size, char* out, size_t out_cap) {
    CodXmodelLod slots[4];
    int n, i;
    if (!out || out_cap == 0) return 0;
    out[0] = '\0';
    n = cod_xmodel_lod_slots(data, size, slots, 4);
    for (i = 0; i < n; i++) {
        if (slots[i].name[0]) {
            strncpy(out, slots[i].name, out_cap - 1);
            out[out_cap - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

int cod_xmodel_lod_names(const void* data, size_t size, char names[][64], int cap) {
    CodXmodelLod slots[4];
    int n_slots, i, n = 0;
    if (!names || cap <= 0) return 0;
    n_slots = cod_xmodel_lod_slots(data, size, slots, 4);
    for (i = 0; i < n_slots && n < cap; i++) {
        if (!slots[i].name[0]) continue;
        strncpy(names[n], slots[i].name, 63);
        names[n][63] = '\0';
        n++;
    }
    return n;
}

typedef struct {
    int parent;
    float trans[3];
    float q[4];
} CoD1Bone;

static void quat_mul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void quat_rot(const float q[4], const float v[3], float o[3]) {
    float tx = 2.f * (q[1] * v[2] - q[2] * v[1]);
    float ty = 2.f * (q[2] * v[0] - q[0] * v[2]);
    float tz = 2.f * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static void bone_identity(CoD1Bone* b) {
    b->trans[0] = b->trans[1] = b->trans[2] = 0.f;
    b->q[0] = b->q[1] = b->q[2] = 0.f;
    b->q[3] = 1.f;
}

static void bone_copy(const CoD1Bone* src, CoD1Bone* dst) {
    dst->parent = src->parent;
    dst->trans[0] = src->trans[0];
    dst->trans[1] = src->trans[1];
    dst->trans[2] = src->trans[2];
    dst->q[0] = src->q[0];
    dst->q[1] = src->q[1];
    dst->q[2] = src->q[2];
    dst->q[3] = src->q[3];
}

static void bone_compose_world(const CoD1Bone* parent, const CoD1Bone* local, CoD1Bone* world) {
    float nt[3], nq[4];
    quat_rot(parent->q, local->trans, nt);
    world->trans[0] = nt[0] + parent->trans[0];
    world->trans[1] = nt[1] + parent->trans[1];
    world->trans[2] = nt[2] + parent->trans[2];
    quat_mul(parent->q, local->q, nq);
    world->q[0] = nq[0];
    world->q[1] = nq[1];
    world->q[2] = nq[2];
    world->q[3] = nq[3];
}

static int resolve_bone_parent(unsigned char parent_byte, int bone_index, CodXmodelBoneRule rule) {
    if (parent_byte == 255 || parent_byte == (unsigned char)bone_index) return -1;
    if (rule.parent0_is_root && parent_byte == 0) return -1;
    return (int)parent_byte;
}

static int read_cod1_bone_local(const unsigned char* parts, size_t parts_size, int bi, CoD1Bone* out) {
    size_t b_off = 6 + (size_t)bi * 19;
    short qx, qy, qz;
    float x, y, z, w2;
    unsigned char parent_byte;

    if (b_off + 19 > parts_size) return 0;
    parent_byte = parts[b_off];
    out->parent = resolve_bone_parent(parent_byte, bi, g_bone_rule);
    b_off += 1;
    memcpy(&out->trans[0], parts + b_off, 4);
    b_off += 4;
    memcpy(&out->trans[1], parts + b_off, 4);
    b_off += 4;
    memcpy(&out->trans[2], parts + b_off, 4);
    b_off += 4;
    qx = (short)(parts[b_off] | (parts[b_off + 1] << 8));
    qy = (short)(parts[b_off + 2] | (parts[b_off + 3] << 8));
    qz = (short)(parts[b_off + 4] | (parts[b_off + 5] << 8));
    x = (float)qx / 32767.f;
    y = (float)qy / 32767.f;
    z = (float)qz / 32767.f;
    w2 = 1.f - x * x - y * y - z * z;
    out->q[0] = x;
    out->q[1] = y;
    out->q[2] = z;
    out->q[3] = w2 > 0.f ? sqrtf(w2) : 0.f;
    return 1;
}

typedef struct {
    char* on_stack;
    char* baked;
    int* stack;
    int stack_len;
} BoneBakeCtx;

static void bake_bone_dfs(int i, int num_bones, const CoD1Bone* locals, CoD1Bone* worlds, BoneBakeCtx* ctx) {
    int p;
    int k;

    if (ctx->baked[i]) return;
    if (ctx->on_stack[i]) {
        for (k = 0; k < ctx->stack_len; k++) {
            int j = ctx->stack[k];
            bone_identity(&worlds[j]);
            ctx->baked[j] = 1;
        }
        return;
    }
    ctx->on_stack[i] = 1;
    ctx->stack[ctx->stack_len++] = i;
    p = locals[i].parent;
    if (p >= 0 && p < num_bones) {
        bake_bone_dfs(p, num_bones, locals, worlds, ctx);
        if (ctx->baked[i]) {
            ctx->stack_len--;
            ctx->on_stack[i] = 0;
            return;
        }
        bone_compose_world(&worlds[p], &locals[i], &worlds[i]);
    } else if (p >= num_bones) {
        bone_identity(&worlds[i]);
    } else {
        bone_copy(&locals[i], &worlds[i]);
    }
    ctx->stack_len--;
    ctx->on_stack[i] = 0;
    ctx->baked[i] = 1;
}

static void bake_bone_worlds(const CoD1Bone* locals, CoD1Bone* worlds, int num_bones) {
    BoneBakeCtx ctx;
    int i;

    if (num_bones <= 0) return;
    ctx.on_stack = (char*)calloc((size_t)num_bones, 1);
    ctx.baked = (char*)calloc((size_t)num_bones, 1);
    ctx.stack = (int*)malloc((size_t)num_bones * sizeof(int));
    ctx.stack_len = 0;
    if (!ctx.on_stack || !ctx.baked || !ctx.stack) {
        free(ctx.on_stack);
        free(ctx.baked);
        free(ctx.stack);
        return;
    }
    for (i = 0; i < num_bones; i++) {
        if (!ctx.baked[i]) bake_bone_dfs(i, num_bones, locals, worlds, &ctx);
    }
    free(ctx.on_stack);
    free(ctx.baked);
    free(ctx.stack);
}

static int cod_xmodel_load_bones(const unsigned char* parts, size_t parts_size, CoD1Bone** out_locals,
                                 CoD1Bone** out_worlds, unsigned short* out_num_bones) {
    size_t p_off = 0;
    unsigned short p_ver = 0, p_num_bones = 0, p_unk = 0;
    CoD1Bone* locals = NULL;
    CoD1Bone* worlds = NULL;
    unsigned short bi;

    *out_locals = NULL;
    *out_worlds = NULL;
    *out_num_bones = 0;
    if (!parts || parts_size < 6) return 0;
    if (!ru16(parts, parts_size, &p_off, &p_ver) || !ru16(parts, parts_size, &p_off, &p_num_bones) ||
        !ru16(parts, parts_size, &p_off, &p_unk))
        return 0;
    (void)p_unk;
    if (p_ver != 14 || p_num_bones == 0 || p_num_bones > 256) return 0;
    locals = (CoD1Bone*)calloc(p_num_bones, sizeof(CoD1Bone));
    worlds = (CoD1Bone*)calloc(p_num_bones, sizeof(CoD1Bone));
    if (!locals || !worlds) {
        free(locals);
        free(worlds);
        return 0;
    }
    for (bi = 0; bi < p_num_bones; bi++) {
        if (!read_cod1_bone_local(parts, parts_size, (int)bi, &locals[bi])) {
            free(locals);
            free(worlds);
            return 0;
        }
    }
    bake_bone_worlds(locals, worlds, (int)p_num_bones);
    *out_locals = locals;
    *out_worlds = worlds;
    *out_num_bones = p_num_bones;
    return 1;
}

int cod_xmodel_world_translations(const void* parts, size_t size, float* out_xyz, int cap) {
    CoD1Bone* locals = NULL;
    CoD1Bone* worlds = NULL;
    unsigned short num_bones = 0;
    int i;

    if (!out_xyz || cap <= 0) return 0;
    if (!cod_xmodel_load_bones((const unsigned char*)parts, size, &locals, &worlds, &num_bones)) return 0;
    if ((int)num_bones > cap) {
        free(locals);
        free(worlds);
        return 0;
    }
    for (i = 0; i < (int)num_bones; i++) {
        out_xyz[i * 3 + 0] = worlds[i].trans[0];
        out_xyz[i * 3 + 1] = worlds[i].trans[1];
        out_xyz[i * 3 + 2] = worlds[i].trans[2];
    }
    free(locals);
    free(worlds);
    return (int)num_bones;
}

/* Retail CoD1 surfs. Rigid verts with bone > 0 are translated by xmodelparts bind pose. */
static int emit_surfs_v14(const unsigned char* p, size_t size, size_t off,
                          const unsigned char* parts, size_t parts_size, Xsb* sb) {
    unsigned short num_meshes = 0;
    unsigned short m;
    int vert_base = 1;
    CoD1Bone* bones = NULL;
    CoD1Bone* bone_locals = NULL;
    unsigned short p_num_bones = 0;

    if (parts && parts_size >= 6 &&
        cod_xmodel_load_bones(parts, parts_size, &bone_locals, &bones, &p_num_bones)) {
        free(bone_locals);
        bone_locals = NULL;
    }

    if (!ru16(p, size, &off, &num_meshes)) {
        free(bones);
        return 0;
    }
    if (num_meshes == 0 || num_meshes > 64) {
        free(bones);
        return 0;
    }

    for (m = 0; m < num_meshes; m++) {
        unsigned short num_verts = 0, num_tris = 0, pad = 0, bone = 0;
        if (g_nmat > 0) {
            int idx = (int)m < g_nmat ? (int)m : g_nmat - 1;
            if (!xsb_append_f(sb, "usemtl %s\n", g_mats[idx])) {
                free(bones);
                return 0;
            }
        } else if (!xsb_append_f(sb, "usemtl skins/unbound.png\n")) {
            free(bones);
            return 0;
        }
        unsigned short* tris = NULL;
        unsigned short* wcs = NULL;
        int ntri = 0;
        int stored = 0;
        int rigged;
        int guard = 0;
        unsigned short v;
        int ok = 1;
        float tx = 0.f, ty = 0.f, tz = 0.f;
        const float* rq = NULL;

        if (!need(off, 1, size)) { free(bones); return 0; }
        off++; /* unknown */
        if (!ru16(p, size, &off, &num_verts) || !ru16(p, size, &off, &num_tris)) { free(bones); return 0; }
        if (!ru16(p, size, &off, &pad) || !ru16(p, size, &off, &bone)) { free(bones); return 0; }
        (void)pad;
        if (num_verts == 0 || num_tris == 0 || num_verts > 65535) { free(bones); return 0; }
        rigged = (bone == 65535);
        if (rigged) {
            if (!need(off, 4, size)) { free(bones); return 0; }
            off += 4;
        } else if (bones && bone > 0 && bone <= p_num_bones) {
            tx = bones[bone - 1].trans[0];
            ty = bones[bone - 1].trans[1];
            tz = bones[bone - 1].trans[2];
            rq = bones[bone - 1].q;
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

        {
            float* xyz = (float*)malloc((size_t)num_verts * 3 * sizeof(float));
            float* nrm = (float*)malloc((size_t)num_verts * 3 * sizeof(float));
            float* uvs = (float*)malloc((size_t)num_verts * 2 * sizeof(float));
            if (!xyz || !nrm || !uvs) {
                free(xyz);
                free(nrm);
                free(uvs);
                free(tris);
                free(wcs);
                free(bones);
                return 0;
            }
            for (v = 0; ok && v < num_verts; v++) {
                unsigned short wc = 0, vb = 0;
                if (!rf32(p, size, &off, &nrm[v * 3]) || !rf32(p, size, &off, &nrm[v * 3 + 1]) ||
                    !rf32(p, size, &off, &nrm[v * 3 + 2]))
                    ok = 0;
                if (!ok || !rf32(p, size, &off, &uvs[v * 2]) || !rf32(p, size, &off, &uvs[v * 2 + 1])) ok = 0;
                if (ok && rigged) {
                    if (!ru16(p, size, &off, &wc) || !ru16(p, size, &off, &vb)) ok = 0;
                    if (wc > 16) ok = 0;
                    (void)vb;
                    wcs[v] = wc;
                }
                if (!ok || !rf32(p, size, &off, &xyz[v * 3]) || !rf32(p, size, &off, &xyz[v * 3 + 1]) ||
                    !rf32(p, size, &off, &xyz[v * 3 + 2]))
                    ok = 0;
                if (ok && rigged && wc) {
                    if (!need(off, 4, size)) ok = 0;
                    else off += 4;
                }
            }
            if (ok && rigged) {
                for (v = 0; ok && v < num_verts; v++) {
                    unsigned short w;
                    float acc[3], ws;
                    acc[0] = acc[1] = acc[2] = 0.f;
                    ws = 0.f;
                    for (w = 0; ok && w < wcs[v]; w++) {
                        unsigned short bone_i = 0;
                        float wt = 0.f, o[3], bp[3];
                        int bi;
                        if (!ru16(p, size, &off, &bone_i) || !rf32(p, size, &off, &wt) ||
                            !rf32(p, size, &off, &o[0]) || !rf32(p, size, &off, &o[1]) ||
                            !rf32(p, size, &off, &o[2])) {
                            ok = 0;
                            break;
                        }
                        bi = (int)bone_i;
                        if (bones && bi >= 0 && bi < (int)p_num_bones && wt > 0.f) {
                            quat_rot(bones[bi].q, o, bp);
                            acc[0] += (bp[0] + bones[bi].trans[0]) * wt;
                            acc[1] += (bp[1] + bones[bi].trans[1]) * wt;
                            acc[2] += (bp[2] + bones[bi].trans[2]) * wt;
                            ws += wt;
                        }
                    }
                    if (ok && ws > 0.001f) {
                        xyz[v * 3] = acc[0] / ws;
                        xyz[v * 3 + 1] = acc[1] / ws;
                        xyz[v * 3 + 2] = acc[2] / ws;
                    }
                }
            }
            for (v = 0; ok && v < num_verts; v++) {
                float px = xyz[v * 3], py = xyz[v * 3 + 1], pz = xyz[v * 3 + 2];
                float nx = nrm[v * 3], ny = nrm[v * 3 + 1], nz = nrm[v * 3 + 2];
                if (rq) {
                    float ip[3], in[3], op[3], on[3];
                    ip[0] = px;
                    ip[1] = py;
                    ip[2] = pz;
                    in[0] = nx;
                    in[1] = ny;
                    in[2] = nz;
                    quat_rot(rq, ip, op);
                    quat_rot(rq, in, on);
                    px = op[0];
                    py = op[1];
                    pz = op[2];
                    nx = on[0];
                    ny = on[1];
                    nz = on[2];
                }
                if (!xsb_append_f(sb, "v %.6g %.6g %.6g\n", px + tx, py + ty, pz + tz)) ok = 0;
                if (ok && !xsb_append_f(sb, "vt %.6g %.6g\n", uvs[v * 2], uvs[v * 2 + 1])) ok = 0;
                if (ok && !xsb_append_f(sb, "vn %.6g %.6g %.6g\n", nx, ny, nz)) ok = 0;
            }
            free(xyz);
            free(nrm);
            free(uvs);
        }
        if (!ok) {
            free(tris);
            free(wcs);
            free(bones);
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
                    free(bones);
                    return 0;
                }
            }
        }
        vert_base += (int)num_verts;
        free(tris);
        free(wcs);
    }
    free(bones);
    return 1;
}

static int emit_surfs(const unsigned char* p, size_t size,
                      const unsigned char* parts, size_t parts_size, Xsb* sb) {
    size_t off = 0;
    unsigned short ver = 0, num_meshes = 0;
    unsigned short m;
    int vert_base = 1; /* OBJ is 1-based across meshes */

    if (!ru16(p, size, &off, &ver)) return 0;
    if (ver == 14) return emit_surfs_v14(p, size, off, parts, parts_size, sb);
    if (g_nmat > 0 && !xsb_append_f(sb, "usemtl %s\n", g_mats[0])) return 0;
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

static int image_ext(const char* s, size_t n) {
    if (n < 4) return 0;
    return cod_strcasecmp(s + n - 4, ".dds") == 0 || cod_strcasecmp(s + n - 4, ".tga") == 0 ||
           cod_strcasecmp(s + n - 4, ".jpg") == 0 || cod_strcasecmp(s + n - 4, ".png") == 0;
}

static int scan_image_cstrings(const unsigned char* p, size_t size, size_t* starts, size_t* lens, int max) {
    size_t i = 0;
    int n = 0;
    while (i < size && n < max) {
        if (p[i] >= 32 && p[i] < 127) {
            size_t start = i;
            while (i < size && p[i] >= 32 && p[i] < 127) i++;
            if (i < size && p[i] == 0) {
                size_t len = i - start;
                if (image_ext((const char*)p + start, len)) {
                    starts[n] = start;
                    lens[n] = len;
                    n++;
                }
            }
        } else {
            i++;
            continue;
        }
        i++;
    }
    return n;
}

int cod_xmodel_skin_names(const void* xmodel, size_t size, char names[][160], int cap) {
    const unsigned char* p = (const unsigned char*)xmodel;
    size_t starts[64];
    size_t lens[64];
    int n;
    int run_start;
    int out = 0;
    int i;

    if (!names || cap <= 0) return 0;
    n = scan_image_cstrings(p, size, starts, lens, 64);
    if (n == 0) return 0;
    run_start = n - 1;
    for (i = n - 1; i > 0; i--) {
        if (starts[i - 1] + lens[i - 1] + 1 == starts[i])
            run_start = i - 1;
        else
            break;
    }
    for (i = run_start; i < n && out < cap; i++) {
        if (lens[i] + 1 > 160) continue;
        memcpy(names[out], p + starts[i], lens[i]);
        names[out][lens[i]] = '\0';
        out++;
    }
    return out;
}

static void store_mat(const char* pick) {
    size_t n;
    char tmp[160];
    int slash;
    if (g_nmat >= 64 || !pick || !pick[0]) return;
    n = strlen(pick);
    if (n + 1 > sizeof(tmp)) return;
    memcpy(tmp, pick, n + 1);
    slash = strchr(tmp, '/') != NULL || strchr(tmp, '\\') != NULL;
    if (n >= 4) memcpy(tmp + n - 4, ".png", 4);
    if (!slash) {
        if (n + 7 > sizeof(g_mats[0])) return;
        memcpy(g_mats[g_nmat], "skins/", 6);
        memcpy(g_mats[g_nmat] + 6, tmp, n + 1);
    } else {
        memcpy(g_mats[g_nmat], tmp, n + 1);
    }
    g_nmat++;
}

static void sort_mats_non_hand_first(void) {
    char temp[64][160];
    int n = 0;
    int i;
    for (i = 0; i < g_nmat; i++) {
        if (!strstr(g_mats[i], "hand")) {
            memcpy(temp[n++], g_mats[i], sizeof(temp[0]));
        }
    }
    for (i = 0; i < g_nmat; i++) {
        if (strstr(g_mats[i], "hand")) {
            memcpy(temp[n++], g_mats[i], sizeof(temp[0]));
        }
    }
    memcpy(g_mats, temp, sizeof(temp));
}

static void shader_names(const unsigned char* p, size_t size) {
    char raw[64][160];
    int i;
    int n;
    g_nmat = 0;
    if (!p) return;
    n = cod_xmodel_skin_names(p, size, raw, 64);
    for (i = 0; i < n; i++) store_mat(raw[i]);
    if (g_keep_hands) sort_mats_non_hand_first();
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
        if (strncmp(s, "MATERIAL", 8) == 0) {
            const char* q = strchr(s, '"');
            char mat[128];
            size_t n = 0;
            if (q) {
                q++;
                while (*q && *q != '"' && n + 1 < sizeof(mat)) mat[n++] = *q++;
                mat[n] = '\0';
                if (n && !xsb_append_f(sb, "usemtl %s\n", mat)) ok = 0;
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
                      const void* parts, size_t parts_size,
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
        else {
            /* ok starts at 0. A successful header is what allows the surf emit. */
            ok = 1;
            g_nmat = 0;
            if (xmodel) shader_names((const unsigned char*)xmodel, xmodel_size);
            {
                int mi;
                for (mi = 0; mi < g_nmat; mi++) {
                    if (!xsb_append_f(&sb, "# mat: %s\n", g_mats[mi])) ok = 0;
                }
            }
            if (ok) ok = emit_surfs((const unsigned char*)surfs, surfs_size,
                                    (const unsigned char*)parts, parts_size, &sb);
        }
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
