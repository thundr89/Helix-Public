/*
 * CoD4 PC fastfile world to a Helix map.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * A multiplayer map lives in zone/<lang>/<map>.ff. Collision is the clipmap
 * brush: the axial box plus each beveled side's plane. When the world draw
 * surfaces are present they are the visible skin (their own color map and
 * stored UVs) and the brushes stay as invisible collision. Sky brushes and
 * sky surfaces are not drawn. Draw triangles without a material table are
 * only a fallback when the brush array is absent. Spawn anchors are the
 * mp_dm_spawn and mp_tdm_spawn entities. Pointers that already name an
 * earlier block are not read again. No game source is copied.
 */

#include "cod4_ffmap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { FF_NEG1 = 0xffffffffu, FF_NEG2 = 0xfffffffeu, FF_ZONE_MAX = 256u * 1024u * 1024u };

typedef struct ColorMap {
    char mat[64];
    char image[96];
    float tint[3];
    int blend;
    int world_alpha;
    int cutout;
    unsigned cull;
    int unlit;
    int camera_hidden;
    unsigned image_ref, image_slot_relative, image_slot;
    unsigned world_slot_event,world_slot_add;
} ColorMap;

typedef struct PropSurf {
    const unsigned char* vert;
    unsigned vc;
    const unsigned char* tri;
    unsigned tc;
    char image[256];
    unsigned mat_ptr;
    char mat_name[64];
    int camera_hidden;
} PropSurf;

typedef struct PropModel {
    unsigned asset;
    PropSurf* surfs;
    unsigned surf_n;
    unsigned lod_first;
    unsigned lod_count;
    float* coll;
    unsigned coll_n;
} PropModel;

typedef struct WorldAlloc { unsigned size,alignment,offset,string_offset; } WorldAlloc;

typedef struct Cur {
    const unsigned char* d;
    unsigned n;
    unsigned o;
    ColorMap* colors;
    unsigned color_n;
    unsigned color_cap;
    char* mat_names;
    unsigned mat_n;
    unsigned mat_cap;
    unsigned asset_i;
    unsigned asset_slot;
    unsigned asset_n;
    char* asset_names;
    PropModel* props;
    unsigned prop_n;
    unsigned prop_cap;
    unsigned image_table_prefix;
    int first_model_material;
    WorldAlloc* world_allocs;
    unsigned world_alloc_n,world_alloc_cap;
    int world_tracking,world_valid;
    unsigned last_image_event;
    struct UiExprState* ui_expr;
    struct UiLocalization* ui_widgets;
    int ui_assets;
    struct UiLocalization* ui_localization;
    int virtual_absolute;
    unsigned virtual_position;
} Cur;

typedef struct Buf {
    char* p;
    unsigned n;
    unsigned cap;
} Buf;

typedef struct WorldGeom {
    const unsigned char* idx;
    unsigned idx_n;
    const unsigned char* vert;
    unsigned vert_n;
    const unsigned char* surf;
    unsigned surf_n;
    const unsigned char* cvert;
    unsigned cvert_n;
    const unsigned char* cidx;
    unsigned ctri_n;
    const unsigned char* partitions;
    unsigned partition_n;
    const unsigned char* aabb_trees;
    unsigned aabb_tree_n;
    const unsigned char* brushes;
    unsigned brush_n;
    const unsigned char* cmats;
    unsigned cmat_n;
    const unsigned char* sides;
    unsigned side_n;
    const unsigned char* planes;
    unsigned plane_n;
    unsigned plane_base;
    int planes_linked;
    unsigned char* side_inline;
    const unsigned char* ents;
    unsigned ent_n;
    ColorMap* colors;
    unsigned color_n;
    char* mat_names;
    unsigned mat_n;
    unsigned mat_at_surfs;
    char* world_mat_names;
    unsigned world_mat_n;
    unsigned surf_mat_lo;
    unsigned surf_mat_hi;
    unsigned surf_mat_n;
    int surf_mat_ok;
    PropModel* props;
    unsigned prop_n;
    const unsigned char* draws;
    unsigned draw_n;
    unsigned asset_slot;
    char* asset_names;
    unsigned asset_n;
} WorldGeom;

static unsigned rd_u16(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd_u32(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int rd_i32(const unsigned char* p) {
    return (int)rd_u32(p);
}

static float rd_f32(const unsigned char* p) {
    unsigned u = rd_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int finite_f(float v) { return v == v && v < 1.0e30f && v > -1.0e30f; }

static int is_neg(unsigned v) { return v == FF_NEG1 || v == FF_NEG2; }

static int remain(const Cur* c, unsigned n) { return c->o <= c->n && n <= c->n - c->o; }

static const unsigned char* take(Cur* c, unsigned n) {
    const unsigned char* p;
    if (!remain(c, n)) return NULL;
    p = c->d + c->o;
    c->o += n;
    return p;
}

static int skip_mul(Cur* c, unsigned count, unsigned stride) {
    if (!count || !stride) return 1;
    if (count > 0x7fffffffu / stride) return 0;
    return take(c, count * stride) != NULL;
}

/* TEMP asset headers do not advance the VIRTUAL block. Record only typed
   VIRTUAL allocations; align them once the MaterialMemory address is known. */
static unsigned world_alloc(Cur* c,unsigned alignment,unsigned size) {
    WorldAlloc* grown;
    if(!c->world_tracking || !c->world_valid)return 0;
    if(c->world_alloc_n==c->world_alloc_cap) {
        unsigned cap=c->world_alloc_cap?c->world_alloc_cap*2u:128u;
        if(cap>1000000u){c->world_valid=0;return 0;}
        grown=(WorldAlloc*)realloc(c->world_allocs,cap*sizeof(WorldAlloc));
        if(!grown){c->world_valid=0;return 0;}
        c->world_allocs=grown;c->world_alloc_cap=cap;
    }
    c->world_allocs[c->world_alloc_n]=(WorldAlloc){size,alignment,0,FF_NEG1};
    if(c->virtual_absolute) {
        unsigned pos=c->virtual_position;
        if(pos>0xffffffffu-alignment || pos+alignment-1u>0xffffffffu-size){c->world_valid=0;return 0;}
        pos=(pos+alignment-1u)&~(alignment-1u);
        c->world_allocs[c->world_alloc_n].offset=pos;c->virtual_position=pos+size;
    }
    c->world_alloc_n++;
    return c->world_alloc_n;
}

static int xstr(Cur* c, unsigned ptr) {
    unsigned start=c->o;
    if (!is_neg(ptr)) return 1;
    if (c->o >= c->n) return 0;
    while (c->o < c->n && c->d[c->o] != 0) c->o++;
    if (c->o >= c->n) return 0;
    c->o++;
    {unsigned event=world_alloc(c,1,c->o-start);if(event)c->world_allocs[event-1u].string_offset=start;}
    return 1;
}

/* Typed asset names may contain generated-material punctuation (for example
   @ and $). Never expose a partially written, unterminated name on failure. */
static int stream_str(const Cur* c,char* out,int cap) {
    unsigned i;int n=0;
    if(!c || !out || cap<2)return 0;
    out[0]=0;
    for(i=c->o;i<c->n && c->d[i];i++) {
        unsigned char ch=c->d[i];
        if(ch<32u || ch>=127u || ch=='"' || ch=='\\' || n+1>=cap){out[0]=0;return 0;}
        if(ch>='A' && ch<='Z')ch=(unsigned char)(ch-'A'+'a');
        out[n++]=(char)ch;
    }
    if(i>=c->n || !n){out[0]=0;return 0;}
    out[n]=0;return 1;
}

static int image_ptr(Cur* c, unsigned ptr);

static int shader_body(Cur* c, const unsigned char* b) {
    unsigned name = rd_u32(b);
    unsigned prog = rd_u32(b + 8);
    unsigned size = rd_u16(b + 12);
    if (!xstr(c, name)) return 0;
    if (!prog) return 1;
    return skip_mul(c, size, 4);
}

static int one_pass(Cur* c, const unsigned char* b) {
    unsigned decl = rd_u32(b);
    unsigned vs = rd_u32(b + 4);
    unsigned ps = rd_u32(b + 8);
    unsigned argc = (unsigned)b[12] + (unsigned)b[13] + (unsigned)b[14];
    unsigned args = rd_u32(b + 16);
    unsigned i;
    const unsigned char* raw;
    if (is_neg(decl) && !take(c, 100)) return 0;
    if (is_neg(vs)) {
        const unsigned char* sh = take(c, 16);
        if (!sh || !shader_body(c, sh)) return 0;
    }
    if (is_neg(ps)) {
        const unsigned char* sh = take(c, 16);
        if (!sh || !shader_body(c, sh)) return 0;
    }
    if (!args || !argc) return 1;
    raw = take(c, argc * 8u);
    if (!raw) return 0;
    for (i = 0; i < argc; i++) {
        unsigned typ = rd_u16(raw + i * 8u);
        unsigned val = rd_u32(raw + i * 8u + 4u);
        if ((typ == 1u || typ == 7u) && is_neg(val) && !take(c, 16)) return 0;
    }
    return 1;
}

static int technique(Cur* c, const unsigned char* b) {
    unsigned name = rd_u32(b);
    unsigned pc = rd_u16(b + 6);
    const unsigned char* raw;
    unsigned i;
    if (pc > 64u) return 0;
    raw = pc ? take(c, pc * 20u) : b;
    if (!raw) return 0;
    for (i = 0; i < pc; i++)
        if (!one_pass(c, raw + i * 20u)) return 0;
    return xstr(c, name);
}

static int techset(Cur* c) {
    const unsigned char* b = take(c, 148);
    unsigned i;
    if (!b || !xstr(c, rd_u32(b))) return 0;
    for (i = 0; i < 34u; i++) {
        unsigned tp = rd_u32(b + 12u + i * 4u);
        const unsigned char* tb;
        if (!is_neg(tp)) continue;
        if(c->world_tracking)c->world_valid=0;
        tb = take(c, 8);
        if (!tb || !technique(c, tb)) return 0;
    }
    return 1;
}

static int remember_color(Cur* c, const char* mat, const char* image) {
    ColorMap* grown;
    unsigned i, cap;
    if (!c || !mat || !mat[0] || !image) return 1;
    for (i = 0; i < c->color_n; i++)
        if (strcmp(c->colors[i].mat, mat) == 0) {
            if (image[0] && image[0] != '$') snprintf(c->colors[i].image, sizeof(c->colors[i].image), "%s", image);
            return 1;
        }
    if (c->color_n == c->color_cap) {
        cap = c->color_cap ? c->color_cap * 2u : 64u;
        grown = (ColorMap*)realloc(c->colors, cap * sizeof(ColorMap));
        if (!grown) return 0;
        c->colors = grown;
        c->color_cap = cap;
    }
    memset(&c->colors[c->color_n], 0, sizeof(ColorMap));
    snprintf(c->colors[c->color_n].mat, sizeof(c->colors[c->color_n].mat), "%s", mat);
    snprintf(c->colors[c->color_n].image, sizeof(c->colors[c->color_n].image), "%s", image);
    c->colors[c->color_n].tint[0] = 1.f;
    c->colors[c->color_n].tint[1] = 1.f;
    c->colors[c->color_n].tint[2] = 1.f;
    /* The material state table determines blending, independently of its name. */
    c->colors[c->color_n].blend = 0;
    c->color_n++;
    return 1;
}

static int image_ptr_name(Cur* c, unsigned ptr, char* name, int cap) {
    const unsigned char* b;
    unsigned tex;
    const unsigned char* def;
    int rsize;
    if (name && cap > 0) name[0] = 0;
    c->last_image_event=0;
    if (!is_neg(ptr)) return 1;
    if(ptr==FF_NEG2)c->last_image_event=world_alloc(c,4,4);
    b = take(c, 36);
    if (!b) return 0;
    if (name && is_neg(rd_u32(b + 32))) stream_str(c, name, cap);
    else if(name && cap>0 && c->ui_assets && c->virtual_absolute && c->world_valid) {
        unsigned p=rd_u32(b+32),i;
        if(p>1u && ((p-1u)>>28)==4u)for(i=0;i<c->world_alloc_n;i++) {
            const WorldAlloc* a=c->world_allocs+i;
            if(a->offset==((p-1u)&0x0fffffffu) && a->string_offset!=FF_NEG1) {
                snprintf(name,(size_t)cap,"%s",(const char*)c->d+a->string_offset);break;
            }
        }
    }
    if (!xstr(c, rd_u32(b + 32))) return 0;
    tex = rd_u32(b + 4);
    if (!is_neg(tex)) return 1;
    /* A reusable TEMP loadDef still reserves its inserted pointer in VIRTUAL. */
    if(tex==FF_NEG2)world_alloc(c,4,4);
    def = take(c, 16);
    if (!def) return 0;
    rsize = rd_i32(def + 12);
    if (rsize < 0 || (unsigned)rsize > 32u * 1024u * 1024u) return 0;
    return rsize ? take(c, (unsigned)rsize) != NULL : 1;
}

static int image_ptr(Cur* c, unsigned ptr) { return image_ptr_name(c, ptr, NULL, 0); }

static int water(Cur* c, const unsigned char* b) {
    int m = rd_i32(b + 12);
    int n = rd_i32(b + 16);
    unsigned img = rd_u32(b + 64);
    unsigned cells;
    if (m < 0) m = 0;
    if (n < 0) n = 0;
    if ((unsigned)m > 4096u || (unsigned)n > 4096u) return 0;
    cells = (unsigned)m * (unsigned)n;
    if (rd_u32(b + 4)) {world_alloc(c,4,cells*8u);if(!skip_mul(c,cells,8))return 0;}
    if (rd_u32(b + 8)) {world_alloc(c,4,cells*4u);if(!skip_mul(c,cells,4))return 0;}
    return image_ptr(c, img);
}

static int texdef(Cur* c, const unsigned char* b, const char* mat, unsigned image_slot,unsigned world_event,unsigned world_add) {
    unsigned u = rd_u32(b + 8);
    char image[96];
    if (b[7] == 11) {
        const unsigned char* w;
        if (!is_neg(u)) return 1;
        world_alloc(c,4,68);
        w = take(c, 68);
        return w && water(c, w);
    }
    /* Semantic 2 is the color map. */
    if (b[7] == 2 || (c->ui_assets && b[7] == 0)) {
        if (!image_ptr_name(c, u, image, (int)sizeof(image))) return 0;
        unsigned j;
        if (!remember_color(c, mat, image)) return 0;
        for(j=0;j<c->color_n;j++) if(strcmp(c->colors[j].mat,mat)==0) {
            c->colors[j].image_ref = is_neg(u) ? 0 : u;
            if(image_slot && image[0]) c->colors[j].image_slot_relative = image_slot;
            if(world_event && image[0]) {c->colors[j].world_slot_event=c->last_image_event?c->last_image_event:world_event;c->colors[j].world_slot_add=c->last_image_event?0:world_add;}
        }
        return 1;
    }
    return image_ptr(c, u);
}

static int push_mat_name(Cur* c, const char* mat) {
    char* grown;
    unsigned cap;
    if (c->mat_n == c->mat_cap) {
        cap = c->mat_cap ? c->mat_cap * 2u : 64u;
        if (cap > 100000u) return 0;
        grown = (char*)realloc(c->mat_names, cap * 64u);
        if (!grown) return 0;
        c->mat_names = grown;
        c->mat_cap = cap;
    }
    snprintf(c->mat_names + c->mat_n * 64u, 64, "%s", mat ? mat : "");
    c->mat_n++;
    return 1;
}

static int material(Cur* c) {
    const unsigned char* b = take(c, 80);
    unsigned name_ptr, tc, cc, sc, tech, tt, ct, sb, name_at, name_bytes, texture_slot = 0;
    int first = c->first_model_material;
    c->first_model_material = 0;
    char mat[64];
    if (!b) return 0;
    name_ptr = rd_u32(b);
    mat[0] = 0;
    if (is_neg(name_ptr)) stream_str(c, mat, (int)sizeof(mat));
    if (!push_mat_name(c, mat) || !remember_color(c, mat, "")) return 0;
    { unsigned j; for(j=0;j<c->color_n;j++) if(strcmp(c->colors[j].mat,mat)==0)
        c->colors[j].camera_hidden = b[62] == 3u; }
    name_at = c->o;
    if (!xstr(c, name_ptr)) return 0;
    name_bytes = c->o - name_at;
    tc = b[58];
    cc = b[59];
    sc = b[60];
    tech = rd_u32(b + 64);
    tt = rd_u32(b + 68);
    ct = rd_u32(b + 72);
    sb = rd_u32(b + 76);
    /* Asset headers live in the temporary block. With a reused technique set,
       only the material name precedes this first texture table in the virtual block. */
    if(first && !is_neg(tech) && is_neg(name_ptr))
        texture_slot = (c->image_table_prefix + name_bytes + 3u) & ~3u;
    if (is_neg(tech) && !techset(c)) return 0;
    if (is_neg(tt) && tc) {
        unsigned world_event=world_alloc(c,4,tc*12u);
        const unsigned char* raw = take(c, tc * 12u);
        unsigned i;
        if (!raw) return 0;
        for (i = 0; i < tc; i++)
            if (!texdef(c, raw + i * 12u, mat, texture_slot ? texture_slot + i*12u + 8u : 0,world_event,i*12u+8u)) return 0;
    }
    if (is_neg(ct) && cc) {
        const unsigned char* raw;
        unsigned i;
        if (cc > 64u) return 0;
        world_alloc(c,16,cc*32u);
        raw = take(c, cc * 32u);
        if (!raw) return 0;
        for (i = 0; i < cc; i++) {
            char nm[13];
            unsigned k, j;
            float r, g, b;
            memcpy(nm, raw + i * 32u + 4u, 12u);
            nm[12] = 0;
            for (k = 0; k < 12u; k++)
                if ((unsigned char)nm[k] < 32u || (unsigned char)nm[k] > 126u) nm[k] = 0;
            if (strcmp(nm, "colorTint") != 0) continue;
            r = rd_f32(raw + i * 32u + 16u);
            g = rd_f32(raw + i * 32u + 20u);
            b = rd_f32(raw + i * 32u + 24u);
            if (!finite_f(r) || !finite_f(g) || !finite_f(b)) continue;
            if (r < 0.f || g < 0.f || b < 0.f || r > 8.f || g > 8.f || b > 8.f) continue;
            for (j = 0; j < c->color_n; j++)
                if (strcmp(c->colors[j].mat, mat) == 0) {
                    c->colors[j].tint[0] = r;
                    c->colors[j].tint[1] = g;
                    c->colors[j].tint[2] = b;
                }
        }
    }
    if (is_neg(sb) && sc) {
        const unsigned char* states;
        world_alloc(c,4,sc*8u);
        states=take(c,sc*8u);
        unsigned j, bits, src, dst;
        int alpha;
        if (!states) return 0;
        /* Select a visible technique, not the depth/shadow prepass at slot zero.
           Material.stateBitsEntry is indexed by technique (lit=7, unlit=4, emissive=5). */
        unsigned entry = b[24u + (b[62] == 2u ? 5u : 7u)];
        int unlit = b[62] == 2u;
        if (entry == 255u) { entry = b[24u + 4u]; unlit = 1; }
        if (entry == 255u) { entry = b[24u + 5u]; unlit = 1; }
        if (entry >= sc) entry = 0;
        bits = rd_u32(states + entry * 8u);
        src = bits & 15u;
        dst = (bits >> 4) & 15u;
        alpha = src == 5u && dst == 6u;
        for (j = 0; j < c->color_n; j++) {
            ColorMap* row = &c->colors[j];
            if (strcmp(row->mat, mat) != 0) continue;
            { unsigned first_bits=rd_u32(states);
              row->world_alpha = !(first_bits & 0x800u) ||
                  ((first_bits & 15u)==5u && ((first_bits>>4)&15u)==6u); }
            row->blend = alpha ? 1 :
                (dst == 2u && (src == 2u || src == 5u || src == 10u)) ? 2 :
                ((src == 9u && dst == 1u) || (src == 1u && dst == 3u)) ? 3 : 0;
            row->cutout = (bits & 0x800u) ? 0 : (int)((bits >> 12) & 3u);
            row->cull = (bits >> 14) & 3u;
            row->unlit = unlit;
        }
    }
    return 1;
}

static const char* color_image(const ColorMap* colors, unsigned n, const char* mat);

static char g_inl_name[2048][64];
static unsigned g_inl_n;
static unsigned g_mat_addr[2048];
static char g_mat_addr_name[2048][64];
static unsigned g_mat_addr_n;

static const char* mat_addr_name(unsigned addr) {
    unsigned i;
    for (i = 0; i < g_mat_addr_n; i++)
        if (g_mat_addr[i] == addr) return g_mat_addr_name[i];
    return NULL;
}

static void remember_mat_addr(unsigned addr, const char* name) {
    if (!name || !name[0] || g_mat_addr_n >= 2048u || mat_addr_name(addr)) return;
    g_mat_addr[g_mat_addr_n] = addr;
    snprintf(g_mat_addr_name[g_mat_addr_n], 64, "%s", name);
    g_mat_addr_n++;
}

static const char* known_model_image(const char* name) {
    const char* tail = name ? strrchr(name, '/') : NULL;
    tail = tail ? tail + 1 : name;
    if (!tail) return NULL;
    if (strcmp(tail, "mtl_red_pine") == 0) return "red_pine_col";
    if (strcmp(tail, "mtl_dead_pine_canopy") == 0) return "dead_pine_col";
    if (strcmp(tail, "mtl_grey_oak") == 0) return "tree_grey_oak_col";
    if (strcmp(tail, "mtl_folding_table_metal") == 0) return "folding_table_color";
    if (strcmp(tail, "mtl_pine") == 0) return "pine02_col";
    if (strcmp(tail, "gfx_floodlight_beam_godray85") == 0 || strcmp(tail, "gfx_spotlight_lensflare_godray50") == 0)
        return "godray_streak";
    return NULL;
}

static const ColorMap* color_row(const ColorMap* colors, unsigned n, const char* mat);

/* The host reads *{r,g,b} as a color multiply and *a as an alpha decal. */
static int decorate_model_image(const char* image, const ColorMap* row, char* out, int cap) {
    char path[120];
    int n, tinted;
    if (!image || !image[0] || !out || cap < 16) return 0;
    if (strncmp(image, "images/", 7) == 0 || strncmp(image, "textures/", 9) == 0)
        snprintf(path, sizeof(path), "%s", image);
    else
        snprintf(path, sizeof(path), "images/%s", image);
    tinted = row && (fabsf(row->tint[0]-1.f) > .001f || fabsf(row->tint[1]-1.f) > .001f || fabsf(row->tint[2]-1.f) > .001f);
    n = snprintf(out, (size_t)cap, "%s", path);
    if (n <= 0 || n >= cap) return 0;
    if (row) {
        char flags[160];
        int f = snprintf(flags, sizeof(flags), "%s%s%s%s",
            row->blend == 1 ? "*a" : row->blend == 2 ? "*add" : row->blend == 3 ? "*modulate" : "",
            row->cutout == 1 ? "*cutout=0.003921569" : row->cutout == 2 ? "*cutoutlt=0.5" : row->cutout == 3 ? "*cutout=0.5" : "",
            row->cull == 1 ? "*cull=none" : row->cull == 2 ? "*cull=back" : row->cull == 3 ? "*cull=front" : "",
            row->unlit ? "*unlit" : "");
        if (f < 0 || f >= (int)sizeof(flags)) return 0;
        if (tinted) n += snprintf(out+n, (size_t)(cap-n), "*{%.3f,%.3f,%.3f}", (double)row->tint[0], (double)row->tint[1], (double)row->tint[2]);
        if (n >= cap) return 0;
        n += snprintf(out+n, (size_t)(cap-n), "%s", flags);
    }
    return n > 0 && n < cap;
}

/* World layers retain the existing opacity/depth-offset path until their
   shader input bindings are mapped. Model camera-pass state is independent. */
static int decorate_image(const char* image, const ColorMap* row, char* out, int cap) {
    ColorMap world;
    if(!row)return decorate_model_image(image,NULL,out,cap);
    world=*row;world.blend=row->world_alpha ? 1 : 0;
    world.cutout=0;world.cull=0;world.unlit=0;
    return decorate_model_image(image,&world,out,cap);
}

static void apply_mat_image(Cur* c, PropSurf* s, const char* name) {
    const char* img;
    const char* known;
    const ColorMap* row;
    if (!s || !name || !name[0]) return;
    if (name != s->mat_name) snprintf(s->mat_name, sizeof(s->mat_name), "%s", name);
    row = color_row(c->colors, c->color_n, name);
    s->camera_hidden = row && row->camera_hidden;
    img = color_image(c->colors, c->color_n, name);
    if (!img || !img[0]) {
        known = known_model_image(name);
        img = known ? known : name;
    }
    if (!decorate_model_image(img, row, s->image, (int)sizeof(s->image))) snprintf(s->image, sizeof(s->image), "%s", img);
}

static void resolve_model_images(Cur* c, PropSurf* surfs, unsigned ns, unsigned first) {
    unsigned i,j,base=0;
    int have_base=0,consistent=1;
    if(first<ns && surfs[first].mat_name[0]) {
        for(i=0;i<ns;i++) {
            unsigned p=surfs[i].mat_ptr, candidate;
            if(p<=1u || is_neg(p) || ((p-1u)>>28)!=4u ||
               strcmp(surfs[i].mat_name,surfs[first].mat_name)!=0) continue;
            candidate=((p-1u)&0x0fffffffu);
            if(candidate<first*4u)continue;
            candidate-=first*4u;
            if(have_base && candidate!=base)consistent=0;
            base=candidate;have_base=1;
        }
        if(have_base && consistent) for(i=0;i<c->color_n;i++) {
            ColorMap* row=&c->colors[i];
            if(strcmp(row->mat,surfs[first].mat_name)==0 && row->image_slot_relative)
                row->image_slot=base+row->image_slot_relative;
        }
    }
    for(i=0;i<c->color_n;i++) {
        ColorMap* row=&c->colors[i]; unsigned p=row->image_ref;
        if(row->image[0] || p<=1u || is_neg(p) || ((p-1u)>>28)!=4u)continue;
        for(j=0;j<c->color_n;j++) if(c->colors[j].image_slot==((p-1u)&0x0fffffffu) && c->colors[j].image[0]) {
            snprintf(row->image,sizeof(row->image),"%s",c->colors[j].image);break;
        }
    }
    for(i=0;i<ns;i++) if(surfs[i].mat_name[0])apply_mat_image(c,&surfs[i],surfs[i].mat_name);
}

static const unsigned char* g_surf_vert;
static unsigned g_surf_vc;
static const unsigned char* g_surf_tri;
static unsigned g_surf_tc;
static unsigned g_vert_fill;
static unsigned g_idx_fill;

typedef struct BlockSeg {
    unsigned blk;
    unsigned file;
    unsigned n;
} BlockSeg;
static BlockSeg g_vseg[8192];
static unsigned g_vseg_n;
static BlockSeg g_iseg[8192];
static unsigned g_iseg_n;

static const unsigned char* seg_bytes(const Cur* c, const BlockSeg* segs, unsigned nseg, unsigned blk_off, unsigned bytes) {
    unsigned i;
    if (!c || !bytes) return NULL;
    for (i = 0; i < nseg; i++) {
        unsigned rel;
        if (blk_off < segs[i].blk || bytes > segs[i].n || blk_off - segs[i].blk > segs[i].n - bytes) continue;
        rel = segs[i].file + (blk_off - segs[i].blk);
        if (rel >= c->n || bytes > c->n - rel) return NULL;
        return c->d + rel;
    }
    return NULL;
}

static int surface_model(Cur* c, const unsigned char* b) {
    unsigned vc = rd_u16(b + 2);
    unsigned tc = rd_u16(b + 4);
    unsigned tri = rd_u32(b + 12);
    unsigned blend = rd_u32(b + 24);
    unsigned verts = rd_u32(b + 28);
    unsigned vlc = rd_u32(b + 32);
    unsigned vlist = rd_u32(b + 36);
    int c0 = (short)rd_u16(b + 16);
    int c1 = (short)rd_u16(b + 18);
    int c2 = (short)rd_u16(b + 20);
    int c3 = (short)rd_u16(b + 22);
    int nblend;
    g_surf_vert = NULL;
    g_surf_vc = 0;
    g_surf_tri = NULL;
    g_surf_tc = 0;
    if (c0 < 0) c0 = 0;
    if (c1 < 0) c1 = 0;
    if (c2 < 0) c2 = 0;
    if (c3 < 0) c3 = 0;
    nblend = c0 + 3 * c1 + 5 * c2 + 7 * c3;
    if (is_neg(blend) && nblend && !skip_mul(c, (unsigned)nblend, 2)) return 0;
    if (is_neg(verts)) {
        const unsigned char* raw;
        if (vc > 200000u) return 0;
        raw = vc ? take(c, vc * 32u) : NULL;
        if (vc && !raw) return 0;
        g_surf_vert = raw;
        g_surf_vc = vc;
        if (g_vseg_n < 8192u) {
            g_vseg[g_vseg_n].blk = g_vert_fill;
            g_vseg[g_vseg_n].file = (unsigned)(raw - c->d);
            g_vseg[g_vseg_n].n = vc * 32u;
            g_vseg_n++;
        }
        g_vert_fill += vc * 32u;
    /* Vertex-block pointers reuse an earlier model's inlined vertices. */
    } else if (vc && vc <= 200000u && verts > 1u && ((verts - 1u) >> 28) == 7u) {
        const unsigned char* raw = seg_bytes(c, g_vseg, g_vseg_n, (verts - 1u) & 0x0fffffffu, vc * 32u);
        if (raw) {
            g_surf_vert = raw;
            g_surf_vc = vc;
        }
    }
    if (is_neg(vlist)) {
        const unsigned char* raw;
        unsigned i;
        if (vlc > 4096u) return 0;
        raw = vlc ? take(c, vlc * 12u) : NULL;
        if (vlc && !raw) return 0;
        for (i = 0; i < vlc; i++) {
            unsigned tree = rd_u32(raw + i * 12u + 8u);
            const unsigned char* tb;
            unsigned nc, nodes, lc, leafs;
            if (!is_neg(tree)) continue;
            tb = take(c, 40);
            if (!tb) return 0;
            nc = rd_u32(tb + 24);
            nodes = rd_u32(tb + 28);
            lc = rd_u32(tb + 32);
            leafs = rd_u32(tb + 36);
            if (nodes && !skip_mul(c, nc, 16)) return 0;
            if (leafs && !skip_mul(c, lc, 2)) return 0;
        }
    }
    if (is_neg(tri)) {
        const unsigned char* raw;
        if (tc > 200000u) return 0;
        raw = tc ? take(c, tc * 6u) : NULL;
        if (tc && !raw) return 0;
        g_surf_tri = raw;
        g_surf_tc = tc;
        /* Index-block allocations are 16-byte aligned. A later model can point
           at an earlier model's indices instead of inlining them again. */
        g_idx_fill = (g_idx_fill + 15u) & ~15u;
        if (g_iseg_n < 8192u) {
            g_iseg[g_iseg_n].blk = g_idx_fill;
            g_iseg[g_iseg_n].file = (unsigned)(raw - c->d);
            g_iseg[g_iseg_n].n = tc * 6u;
            g_iseg_n++;
        }
        g_idx_fill += tc * 6u;
    } else if (tc && tc <= 200000u && tri > 1u && ((tri - 1u) >> 28) == 8u) {
        const unsigned char* raw = seg_bytes(c, g_iseg, g_iseg_n, (tri - 1u) & 0x0fffffffu, tc * 6u);
        if (raw) {
            g_surf_tri = raw;
            g_surf_tc = tc;
        }
    }
    return 1;
}

static int brush_wrap(Cur* c, const unsigned char* b) {
    unsigned ns = rd_u32(b + 28);
    unsigned sides = rd_u32(b + 32);
    unsigned base = rd_u32(b + 48);
    int total = rd_i32(b + 72);
    unsigned planes = rd_u32(b + 76);
    if (sides) {
        const unsigned char* sb;
        unsigned i;
        if (ns > 256u) return 0;
        sb = take(c, ns * 12u);
        if (!sb) return 0;
        for (i = 0; i < ns; i++)
            if (is_neg(rd_u32(sb + i * 12u)) && !take(c, 20)) return 0;
    }
    if (base && total > 0 && !take(c, (unsigned)total)) return 0;
    if (is_neg(planes) && !skip_mul(c, ns, 20)) return 0;
    return 1;
}

static const char* color_image(const ColorMap* colors, unsigned n, const char* mat);

/* plane/svec/tvec are affine coordinate equations, not vertex vectors.
   Solve n.p=d, s.p=s.w+u, t.p=t.w+v at (u,v)=(0,0),(1,0),(0,1). */
static int collision_triangle(const unsigned char* raw, float* tri) {
    double m[3][4], inv[3][3], det, scale = 1.0;
    unsigned r, k, v;
    for (r = 0; r < 3; r++) {
        double norm = 0;
        for (k = 0; k < 4; k++) {
            m[r][k] = rd_f32(raw + r * 16u + k * 4u);
            if (!finite_f((float)m[r][k])) return 0;
            if (k < 3) norm += m[r][k] * m[r][k];
        }
        scale *= sqrt(norm);
    }
    for (k = 0; k < 3; k++) {
        unsigned a = (k + 1u) % 3u, b = (k + 2u) % 3u;
        inv[k][0] = m[1][a]*m[2][b] - m[1][b]*m[2][a];
        inv[k][1] = m[2][a]*m[0][b] - m[2][b]*m[0][a];
        inv[k][2] = m[0][a]*m[1][b] - m[0][b]*m[1][a];
    }
    det = m[0][0]*inv[0][0] + m[0][1]*inv[1][0] + m[0][2]*inv[2][0];
    if (!(scale > 0) || fabs(det) <= scale * 1.e-12) return 0;
    for (v = 0; v < 3; v++) {
        for (k = 0; k < 3; k++) {
            double x = (inv[k][0]*m[0][3] + inv[k][1]*(m[1][3] + (v == 1)) +
                        inv[k][2]*(m[2][3] + (v == 2))) / det;
            tri[v*3u+k] = (float)x;
            if (!finite_f(tri[v*3u+k])) return 0;
        }
    }
    /* Preserve the authored outward normal independently of coordinate handedness. */
    if (det < 0) {
        for (k = 0; k < 3; k++) {
            float x = tri[3u+k]; tri[3u+k] = tri[6u+k]; tri[6u+k] = x;
        }
    }
    return 1;
}

static int xmodel(Cur* c) {
    const unsigned char* b = take(c, 220);
    unsigned nb, nroot, ns, extra, i;
    unsigned field[8];
    unsigned coll, binfo, phys, geoms;
    int ncoll;
    PropSurf* surfs = NULL;
    static const unsigned off[8] = {8, 12, 16, 20, 24, 28, 32, 36};
    if (!b) return 0;
    if (!xstr(c, rd_u32(b))) return 0;
    nb = b[4];
    nroot = b[5];
    ns = b[6];
    for (i = 0; i < 8u; i++) field[i] = rd_u32(b + off[i]);
    coll = rd_u32(b + 152);
    ncoll = rd_i32(b + 156);
    binfo = rd_u32(b + 164);
    phys = rd_u32(b + 212);
    geoms = rd_u32(b + 216);
    extra = nb > nroot ? nb - nroot : 0;
    if (is_neg(field[0]) && !skip_mul(c, nb, 2)) return 0;
    if (is_neg(field[1]) && extra && !take(c, extra)) return 0;
    if (is_neg(field[2]) && extra && !skip_mul(c, extra, 8)) return 0;
    if (is_neg(field[3]) && extra && !skip_mul(c, extra, 16)) return 0;
    if (is_neg(field[4]) && nb && !take(c, nb)) return 0;
    if (is_neg(field[5]) && !skip_mul(c, nb, 32)) return 0;
    if (ns > 512u) return 0;
    if (ns) {
        surfs = (PropSurf*)calloc(ns, sizeof(PropSurf));
        if (!surfs) return 0;
    }
    if (field[6] && ns) {
        const unsigned char* sb = take(c, ns * 56u);
        if (!sb) {
            free(surfs);
            return 0;
        }
        for (i = 0; i < ns; i++) {
            if (!surface_model(c, sb + i * 56u)) {
                free(surfs);
                return 0;
            }
            surfs[i].vert = g_surf_vert;
            surfs[i].vc = g_surf_vc;
            surfs[i].tri = g_surf_tri;
            surfs[i].tc = g_surf_tc;
        }
    }
    if (is_neg(field[7]) && ns) {
        const unsigned char* mb = take(c, ns * 4u);
        unsigned inl_base = g_inl_n;
        unsigned first_inline = ns;
        c->image_table_prefix = ns * 4u;
        c->first_model_material = 1;
        unsigned unk[512];
        unsigned unk_n = 0;
        unsigned u, a, b;
        if (!mb) {
            free(surfs);
            return 0;
        }
        for (i = 0; i < ns; i++) {
            unsigned mp = rd_u32(mb + i * 4u);
            const char* name = NULL;
            surfs[i].mat_ptr = mp;
            if (is_neg(mp)) {
                unsigned before = c->mat_n;
                if(first_inline==ns)first_inline=i;
                if (!material(c)) {
                    free(surfs);
                    return 0;
                }
                if (c->mat_n > before) name = c->mat_names + (c->mat_n - 1u) * 64u;
                if (g_inl_n < 2048u) {
                    snprintf(g_inl_name[g_inl_n], 64, "%s", name ? name : "");
                    g_inl_n++;
                }
                apply_mat_image(c, &surfs[i], name);
            } else if (mp > 1u && ((mp - 1u) >> 28) == 4u) {
                const char* known = mat_addr_name((mp - 1u) & 0x0fffffffu);
                if (known) apply_mat_image(c, &surfs[i], known);
            }
        }
        /* Later surfaces point at this model's own materials. Those objects
           are 4 bytes apart in the order the materials were inlined. */
        for (i = 0; i < ns; i++) {
            unsigned mp = surfs[i].mat_ptr;
            unsigned o, k;
            if (surfs[i].image[0] || mp <= 1u || is_neg(mp) || ((mp - 1u) >> 28) != 4u) continue;
            o = (mp - 1u) & 0x0fffffffu;
            for (k = 0; k < unk_n; k++)
                if (unk[k] == o) break;
            if (k == unk_n && unk_n < 512u) unk[unk_n++] = o;
        }
        for (a = 0; a + 1 < unk_n; a++)
            for (b = a + 1; b < unk_n; b++)
                if (unk[b] < unk[a]) {
                    unsigned t = unk[a];
                    unk[a] = unk[b];
                    unk[b] = t;
                }
        u = g_inl_n - inl_base;
        {
            /* Addresses climb in inline order. A 4-byte step is the next
               material; a short multiple of 4 is a material this model did
               not reuse. A far address belongs to some other model. */
            int local = unk_n && u && unk_n <= u;
            unsigned idx_of[512];
            if (local) {
                for (a = 1; a < unk_n; a++) {
                    unsigned gap = unk[a] - unk[a - 1u];
                    if (gap < 4u || gap > 64u || (gap % 4u) != 0u) local = 0;
                }
            }
            if (local && unk_n == u) {
                for (a = 0; a < unk_n; a++) idx_of[a] = a;
            } else if (local && (unk[unk_n - 1u] - unk[0]) / 4u < u) {
                for (a = 0; a < unk_n; a++) idx_of[a] = (unk[a] - unk[0]) / 4u;
            } else
                local = 0;
            if (local) {
                for (a = 0; a < unk_n; a++) remember_mat_addr(unk[a], g_inl_name[inl_base + idx_of[a]]);
                for (i = 0; i < ns; i++) {
                    unsigned mp = surfs[i].mat_ptr;
                    const char* nm;
                    if (surfs[i].image[0] || mp <= 1u || is_neg(mp) || ((mp - 1u) >> 28) != 4u) continue;
                    nm = mat_addr_name((mp - 1u) & 0x0fffffffu);
                    if (nm) apply_mat_image(c, &surfs[i], nm);
                }
            }
        }
        c->first_model_material = 0;
        resolve_model_images(c, surfs, ns, first_inline);
    }
    if (c->prop_n == c->prop_cap) {
        unsigned cap = c->prop_cap ? c->prop_cap * 2u : 16u;
        PropModel* grown = (PropModel*)realloc(c->props, cap * sizeof(PropModel));
        if (!grown) {
            free(surfs);
            return 0;
        }
        c->props = grown;
        c->prop_cap = cap;
    }
    c->props[c->prop_n].asset = c->asset_i;
    c->props[c->prop_n].surfs = surfs;
    c->props[c->prop_n].surf_n = ns;
    /* numsurfs spans every LOD. Export only the highest-detail range;
       drawing all ranges overlays different meshes at the same position. */
    c->props[c->prop_n].lod_first = rd_u16(b + 46);
    c->props[c->prop_n].lod_count = rd_u16(b + 44);
    if (!c->props[c->prop_n].lod_count || c->props[c->prop_n].lod_first > ns ||
        c->props[c->prop_n].lod_count > ns - c->props[c->prop_n].lod_first) {
        c->props[c->prop_n].lod_first = 0;
        c->props[c->prop_n].lod_count = ns;
    }
    c->props[c->prop_n].coll = NULL;
    c->props[c->prop_n].coll_n = 0;
    c->prop_n++;
    if (coll && ncoll > 0) {
        const unsigned char* cb;
        float* tris = NULL;
        unsigned tn = 0, cap = 0;
        if (ncoll > 4096) return 0;
        cb = take(c, (unsigned)ncoll * 44u);
        if (!cb) return 0;
        for (i = 0; i < (unsigned)ncoll; i++) {
            unsigned ct = rd_u32(cb + i * 44u);
            int nt = rd_i32(cb + i * 44u + 4u);
            const unsigned char* raw;
            int t;
            if (!ct || nt <= 0) continue;
            if ((unsigned)nt > 8192u) { free(tris); return 0; }
            raw = take(c, (unsigned)nt * 48u);
            if (!raw) {
                free(tris);
                return 0;
            }
            for (t = 0; t < nt; t++) {
                const unsigned char* rec = raw + (unsigned)t * 48u;
                float tri[9];
                /* Non-solid collision surfaces (foliage, visual-only)
                   must not become player blockers merely because they have triangles. */
                if (!(rd_u32(cb + i * 44u + 36u) & 0x10011u)) continue;
                if (!collision_triangle(rec, tri)) continue;
                if (tn == cap) {
                    unsigned ncap = cap ? cap * 2u : 16u;
                    float* grown = (float*)realloc(tris, ncap * 9u * sizeof(float));
                    if (!grown) { free(tris); return 0; }
                    tris = grown;
                    cap = ncap;
                }
                memcpy(tris + tn * 9u, tri, sizeof(tri));
                tn++;
            }
        }
        c->props[c->prop_n - 1u].coll = tris;
        c->props[c->prop_n - 1u].coll_n = tn;
    }
    if (binfo && !skip_mul(c, nb, 40)) return 0;
    if (is_neg(phys)) {
        const unsigned char* pb = take(c, 44);
        if (!pb || !xstr(c, rd_u32(pb)) || !xstr(c, rd_u32(pb + 28))) return 0;
    }
    if (is_neg(geoms)) {
        const unsigned char* gb = take(c, 44);
        unsigned cnt, gp;
        if (!gb) return 0;
        cnt = rd_u32(gb);
        gp = rd_u32(gb + 4);
        if (gp && cnt) {
            const unsigned char* ib;
            if (cnt > 256u) return 0;
            ib = take(c, cnt * 68u);
            if (!ib) return 0;
            for (i = 0; i < cnt; i++) {
                const unsigned char* br;
                if (!is_neg(rd_u32(ib + i * 68u))) continue;
                br = take(c, 80);
                if (!br || !brush_wrap(c, br)) return 0;
            }
        }
    }
    return 1;
}

/* These assets are not executed here, but their serialized children precede
   the map and must be consumed using the IW3 field order. */
static int raw_file(Cur* c) {
    const unsigned char* b=take(c,12);
    unsigned len;
    if(!b || !xstr(c,rd_u32(b)))return 0;
    len=rd_u32(b+4);
    if(len>=0x7fffffffu)return 0;
    return !is_neg(rd_u32(b+8)) || skip_mul(c,len+1u,1);
}

static int anim_parts(Cur* c) {
    const unsigned char* b=take(c,88);
    unsigned i;
    static const unsigned fields[]={52,56,60,64,68,72,76};
    unsigned counts[7],strides[7]={1,2,4,2,1,4,1};
    if(!b || !xstr(c,rd_u32(b)))return 0;
    if(is_neg(rd_u32(b+48)) && !skip_mul(c,b[27],2))return 0;
    if(is_neg(rd_u32(b+80)) && !skip_mul(c,b[28],8))return 0;
    if(is_neg(rd_u32(b+84))) {
        const unsigned char* d=take(c,8);
        unsigned width=rd_u16(b+14)<256u?1u:2u;
        if(!d)return 0;
        if(is_neg(rd_u32(d))) {
            const unsigned char* h=take(c,4);
            unsigned n;
            if(!h)return 0;
            n=rd_u16(h);
            if(!n) { if(!take(c,12))return 0; }
            else {
                const unsigned char* f=take(c,28);
                if(!f || !skip_mul(c,n+1u,width))return 0;
                if(is_neg(rd_u32(f+24)) && !skip_mul(c,n+1u,h[2]?3u:6u))return 0;
            }
        }
        if(is_neg(rd_u32(d+4))) {
            const unsigned char* h=take(c,4);
            unsigned n;
            if(!h)return 0;
            n=rd_u16(h);
            if(!n) { if(!take(c,4))return 0; }
            else {
                const unsigned char* f=take(c,4);
                if(!f || !skip_mul(c,n+1u,width))return 0;
                if(is_neg(rd_u32(f)) && !skip_mul(c,n+1u,4))return 0;
            }
        }
    }
    counts[0]=rd_u16(b+4); counts[1]=rd_u16(b+6); counts[2]=rd_u16(b+8);
    counts[3]=rd_u32(b+32); counts[4]=rd_u16(b+10); counts[5]=rd_u16(b+12);
    counts[6]=rd_u32(b+36); strides[6]=rd_u16(b+14)<256u?1u:2u;
    for(i=0;i<7;i++)if(is_neg(rd_u32(b+fields[i])) && !skip_mul(c,counts[i],strides[i]))return 0;
    return 1;
}

static int light_def(Cur* c) {
    const unsigned char* b=take(c,16);
    return b && xstr(c,rd_u32(b)) && image_ptr(c,rd_u32(b+4));
}

static int fx_visual(Cur* c, unsigned type, unsigned ptr) {
    if(type==8u || type==10u)return xstr(c,ptr);
    if(!is_neg(ptr))return 1;
    if(type==5u)return xmodel(c);
    if(type<=4u)return material(c);
    return type==6u || type==7u;
}

/* FX is not executed by the map importer. Its typed children still have to be
   consumed so the following world asset begins at the correct stream position. */
static int fx_effect(Cur* c) {
    const unsigned char* b=take(c,32);
    const unsigned char* elems;
    unsigned count,i;
    if(!b || !xstr(c,rd_u32(b)))return 0;
    count=rd_u32(b+16);
    if(count>4096u || rd_u32(b+20)>4096u-count)return 0;
    count+=rd_u32(b+20);
    if(rd_u32(b+24)>4096u-count)return 0;
    count+=rd_u32(b+24);
    if(!is_neg(rd_u32(b+28)) || !count)return 1;
    elems=take(c,count*252u);if(!elems)return 0;
    for(i=0;i<count;i++) {
        const unsigned char* e=elems+i*252u;
        unsigned type=e[176],vc=e[177],vp=rd_u32(e+188),j;
        if(type>10u)return 0;
        if(is_neg(rd_u32(e+180)) && !skip_mul(c,(unsigned)e[178]+1u,96))return 0;
        if(is_neg(rd_u32(e+184)) && !skip_mul(c,(unsigned)e[179]+1u,48))return 0;
        if(type==9u) {
            if(is_neg(vp) && vc) {
                const unsigned char* marks=take(c,vc*8u);if(!marks)return 0;
                for(j=0;j<vc*2u;j++)if(is_neg(rd_u32(marks+j*4u)) && !material(c))return 0;
            }
        } else if(vc>1u) {
            if(is_neg(vp)) {
                const unsigned char* visuals=take(c,vc*4u);if(!visuals)return 0;
                for(j=0;j<vc;j++)if(!fx_visual(c,type,rd_u32(visuals+j*4u)))return 0;
            }
        } else if(vc && !fx_visual(c,type,vp))return 0;
        if(!xstr(c,rd_u32(e+216)) || !xstr(c,rd_u32(e+220)) || !xstr(c,rd_u32(e+224)))return 0;
        if(is_neg(rd_u32(e+244))) {
            const unsigned char* t=take(c,28);if(!t)return 0;
            if(rd_u32(t+12)>100000u || rd_u32(t+20)>300000u)return 0;
            if(is_neg(rd_u32(t+16)) && !skip_mul(c,rd_u32(t+12),20))return 0;
            if(is_neg(rd_u32(t+24)) && !skip_mul(c,rd_u32(t+20),2))return 0;
        }
    }
    return 1;
}

static int sound_alias(Cur* c) {
    const unsigned char* b=take(c,12),*a;
    unsigned count,i,j;
    if(!b || !xstr(c,rd_u32(b)))return 0;
    count=rd_u32(b+8);
    if(count>4096u)return 0;
    if(!is_neg(rd_u32(b+4)))return 1;
    a=take(c,count*92u);if(!a)return 0;
    for(i=0;i<count;i++) {
        const unsigned char* e=a+i*92u;
        for(j=0;j<4;j++)if(!xstr(c,rd_u32(e+j*4u)))return 0;
        if(is_neg(rd_u32(e+16))) {
            const unsigned char* f=take(c,12);if(!f)return 0;
            if(f[0]==2) {
                if(!xstr(c,rd_u32(f+4)) || !xstr(c,rd_u32(f+8)))return 0;
            } else if(f[0]==1 && is_neg(rd_u32(f+4))) {
                const unsigned char* l=take(c,44);
                if(!l || !xstr(c,rd_u32(l)))return 0;
                if(is_neg(rd_u32(l+40)) && !skip_mul(c,rd_u32(l+12),1))return 0;
            } else if(f[0]!=1)return 0;
        }
        if(is_neg(rd_u32(e+72))) {
            const unsigned char* curve=take(c,72);
            if(!curve || !xstr(c,rd_u32(curve)))return 0;
        }
        if(is_neg(rd_u32(e+88))) {
            const unsigned char* speaker=take(c,408);
            if(!speaker || !xstr(c,rd_u32(speaker+4)))return 0;
        }
    }
    return 1;
}

static int sound_name(Cur* c, unsigned ptr) {
    const unsigned char* b;
    if(!is_neg(ptr))return 1;
    b=take(c,4);
    return b && xstr(c,rd_u32(b));
}

/* IW3 WeaponDef pointer fields, in serialization order. Scalar tuning values
   remain opaque: this advances assets, it does not implement weapon gameplay. */
static int weapon_def(Cur* c) {
    static const unsigned short fields[][3]={
        {0,1,0}, /* szInternalName */
        {4,1,0}, /* szDisplayName */
        {8,1,0}, /* szOverlayName */
        {12,16,1}, /* gunXModel */
        {76,1,1}, /* handXModel */
        {80,33,0}, /* szXAnims */
        {212,1,0}, /* szModeName */
        {332,1,2}, /* viewFlashEffect */
        {336,1,2}, /* worldFlashEffect */
        {340,1,4}, /* pickupSound */
        {344,1,4}, /* pickupSoundPlayer */
        {348,1,4}, /* ammoPickupSound */
        {352,1,4}, /* ammoPickupSoundPlayer */
        {356,1,4}, /* projectileSound */
        {360,1,4}, /* pullbackSound */
        {364,1,4}, /* pullbackSoundPlayer */
        {368,1,4}, /* fireSound */
        {372,1,4}, /* fireSoundPlayer */
        {376,1,4}, /* fireLoopSound */
        {380,1,4}, /* fireLoopSoundPlayer */
        {384,1,4}, /* fireStopSound */
        {388,1,4}, /* fireStopSoundPlayer */
        {392,1,4}, /* fireLastSound */
        {396,1,4}, /* fireLastSoundPlayer */
        {400,1,4}, /* emptyFireSound */
        {404,1,4}, /* emptyFireSoundPlayer */
        {408,1,4}, /* meleeSwipeSound */
        {412,1,4}, /* meleeSwipeSoundPlayer */
        {416,1,4}, /* meleeHitSound */
        {420,1,4}, /* meleeMissSound */
        {424,1,4}, /* rechamberSound */
        {428,1,4}, /* rechamberSoundPlayer */
        {432,1,4}, /* reloadSound */
        {436,1,4}, /* reloadSoundPlayer */
        {440,1,4}, /* reloadEmptySound */
        {444,1,4}, /* reloadEmptySoundPlayer */
        {448,1,4}, /* reloadStartSound */
        {452,1,4}, /* reloadStartSoundPlayer */
        {456,1,4}, /* reloadEndSound */
        {460,1,4}, /* reloadEndSoundPlayer */
        {464,1,4}, /* detonateSound */
        {468,1,4}, /* detonateSoundPlayer */
        {472,1,4}, /* nightVisionWearSound */
        {476,1,4}, /* nightVisionWearSoundPlayer */
        {480,1,4}, /* nightVisionRemoveSound */
        {484,1,4}, /* nightVisionRemoveSoundPlayer */
        {488,1,4}, /* altSwitchSound */
        {492,1,4}, /* altSwitchSoundPlayer */
        {496,1,4}, /* raiseSound */
        {500,1,4}, /* raiseSoundPlayer */
        {504,1,4}, /* firstRaiseSound */
        {508,1,4}, /* firstRaiseSoundPlayer */
        {512,1,4}, /* putawaySound */
        {516,1,4}, /* putawaySoundPlayer */
        {520,1,5}, /* bounceSound */
        {524,1,2}, /* viewShellEjectEffect */
        {528,1,2}, /* worldShellEjectEffect */
        {532,1,2}, /* viewLastShotEjectEffect */
        {536,1,2}, /* worldLastShotEjectEffect */
        {540,1,3}, /* reticleCenter */
        {544,1,3}, /* reticleSide */
        {700,16,1}, /* worldModel */
        {764,1,1}, /* worldClipModel */
        {768,1,1}, /* rocketModel */
        {772,1,1}, /* knifeModel */
        {776,1,1}, /* worldKnifeModel */
        {780,1,3}, /* hudIcon */
        {788,1,3}, /* ammoCounterIcon */
        {804,1,0}, /* szAmmoName */
        {812,1,0}, /* szClipName */
        {832,1,0}, /* szSharedAmmoCapName */
        {1072,1,3}, /* overlayMaterial */
        {1076,1,3}, /* overlayMaterialLowRes */
        {1304,1,3}, /* killIcon */
        {1316,1,3}, /* dpadIcon */
        {1340,1,0}, /* szAltWeaponName */
        {1412,1,1}, /* projectileModel */
        {1420,1,2}, /* projExplosionEffect */
        {1428,1,2}, /* projDudEffect */
        {1432,1,4}, /* projExplosionSound */
        {1436,1,4}, /* projDudSound */
        {1704,1,2}, /* projTrailEffect */
        {1732,1,2}, /* projIgnitionEffect */
        {1736,1,4}, /* projIgnitionSound */
        {1900,1,0}, /* aiVsAiAccuracyGraphName */
        {1908,1,6}, /* aiVsAiAccuracyGraphKnots */
        {1916,1,6}, /* originalAiVsAiAccuracyGraphKnots */
        {1904,1,0}, /* aiVsPlayerAccuracyGraphName */
        {1912,1,6}, /* aiVsPlayerAccuracyGraphKnots */
        {1920,1,6}, /* originalAiVsPlayerAccuracyGraphKnots */
        {2012,1,0}, /* szUseHintString */
        {2016,1,0}, /* dropHintString */
        {2036,1,0}, /* szScript */
        {2152,1,0}, /* fireRumble */
        {2156,1,0}, /* meleeImpactRumble */
    };
    const unsigned char* b=take(c,2168);
    unsigned i,j;
    if(!b)return 0;
    for(i=0;i<sizeof(fields)/sizeof(fields[0]);i++)for(j=0;j<fields[i][1];j++) {
        unsigned off=fields[i][0], kind=fields[i][2],ptr=rd_u32(b+off+j*4u);
        if(kind==0) { if(!xstr(c,ptr))return 0; }
        else if(kind==4) { if(!sound_name(c,ptr))return 0; }
        else if(is_neg(ptr)) {
            if(kind==1) { if(!xmodel(c))return 0; }
            else if(kind==2) { if(!fx_effect(c))return 0; }
            else if(kind==3) { if(!material(c))return 0; }
            else if(kind==5) {
                const unsigned char* a=take(c,29u*4u);
                unsigned k;
                if(!a)return 0;
                for(k=0;k<29;k++)if(!sound_name(c,rd_u32(a+k*4u)))return 0;
            } else {
                unsigned count=rd_u32(b+((off==1908u || off==1916u)?1924u:1928u));
                if(!skip_mul(c,count,8))return 0;
            }
        }
    }
    return 1;
}

static int comworld(Cur* c) {
    const unsigned char* b = take(c, 16);
    unsigned count, lights, i;
    const unsigned char* lb;
    if (!b || !xstr(c, rd_u32(b))) return 0;
    count = rd_u32(b + 8);
    lights = rd_u32(b + 12);
    if (!lights || !count) return 1;
    if (count > 4096u) return 0;
    lb = take(c, count * 68u);
    if (!lb) return 0;
    for (i = 0; i < count; i++)
        if (!xstr(c, rd_u32(lb + i * 68u + 64u))) return 0;
    return 1;
}

static int cell_body(Cur* c, const unsigned char* b, int depth) {
    int trees_n = rd_i32(b + 24);
    unsigned trees = rd_u32(b + 28);
    int portals_n = rd_i32(b + 32);
    unsigned portals = rd_u32(b + 36);
    int cull_n = rd_i32(b + 40);
    unsigned culls = rd_u32(b + 44);
    unsigned rpc = b[48];
    unsigned rpp = rd_u32(b + 52);
    if (depth > 32) return 0;
    if (trees && trees_n > 0) {
        const unsigned char* tb;
        int i;
        if (trees_n > 100000) return 0;
        tb = take(c, (unsigned)trees_n * 44u);
        if (!tb) return 0;
        for (i = 0; i < trees_n; i++) {
            unsigned smc = rd_u16(tb + (unsigned)i * 44u + 34u);
            unsigned smp = rd_u32(tb + (unsigned)i * 44u + 36u);
            if (is_neg(smp) && smc && !skip_mul(c, smc, 2)) return 0;
        }
    }
    if (portals && portals_n > 0) {
        const unsigned char* pb;
        int i;
        if (portals_n > 100000) return 0;
        pb = take(c, (unsigned)portals_n * 68u);
        if (!pb) return 0;
        for (i = 0; i < portals_n; i++) {
            unsigned cellp = rd_u32(pb + (unsigned)i * 68u + 32u);
            unsigned verts = rd_u32(pb + (unsigned)i * 68u + 36u);
            unsigned vc = pb[(unsigned)i * 68u + 40u];
            if (is_neg(cellp)) {
                const unsigned char* cb = take(c, 56);
                if (!cb || !cell_body(c, cb, depth + 1)) return 0;
            }
            if (verts && vc && !skip_mul(c, vc, 12)) return 0;
        }
    }
    if (culls && cull_n > 0 && !skip_mul(c, (unsigned)cull_n, 4)) return 0;
    if (rpp && rpc && !take(c, rpc)) return 0;
    return 1;
}

static void resolve_world_images(Cur* c,unsigned base) {
    unsigned i,j,pos=base;
    if(!c->world_valid)return;
    for(i=0;i<c->world_alloc_n;i++) {
        WorldAlloc* a=c->world_allocs+i;
        if(pos>0xffffffffu-a->alignment || pos+a->alignment-1u>0xffffffffu-a->size)return;
        pos=(pos+a->alignment-1u)&~(a->alignment-1u);a->offset=pos;pos+=a->size;
    }
    for(i=0;i<c->color_n;i++) {
        ColorMap* row=c->colors+i;
        if(row->world_slot_event && row->world_slot_event<=c->world_alloc_n)
            row->image_slot=c->world_allocs[row->world_slot_event-1u].offset+row->world_slot_add;
    }
    for(i=0;i<c->color_n;i++) {
        ColorMap* row=c->colors+i;unsigned p=row->image_ref;
        if(row->image[0] || p<=1u || is_neg(p) || ((p-1u)>>28)!=4u)continue;
        for(j=0;j<c->color_n;j++)if(c->colors[j].image_slot==((p-1u)&0x0fffffffu) && c->colors[j].image[0]) {
            snprintf(row->image,sizeof(row->image),"%s",c->colors[j].image);break;
        }
    }
}

static int load_gfx(Cur* c, WorldGeom* w) {
    const unsigned char* b = take(c, 732);
    unsigned index_count, vert_count, surf_count, sky_n, refl_n, cell_n, lmap_n, model_n, mem_n;
    unsigned i;
    const unsigned char* grid;
    const unsigned char* dpvs;
    if (!b || !xstr(c, rd_u32(b)) || !xstr(c, rd_u32(b + 4))) return 0;
    index_count = (unsigned)rd_i32(b + 16);
    vert_count = rd_u32(b + 48);
    surf_count = (unsigned)rd_i32(b + 24);
    if (index_count > 4000000u || vert_count > 2000000u || surf_count > 200000u) return 0;
    if (rd_u32(b + 20)) {
        const unsigned char* idx = take(c, index_count * 2u);
        if (!idx && index_count) return 0;
        w->idx = idx;
        w->idx_n = index_count;
    }
    sky_n = (unsigned)rd_i32(b + 32);
    if (rd_u32(b + 36)) {
        if (sky_n > 64u || !skip_mul(c, sky_n, 4)) return 0;
    }
    if (!image_ptr(c, rd_u32(b + 40))) return 0;
    if (rd_u32(b + 200)) {
        const unsigned char* light = take(c, 64);
        unsigned def;
        if (!light) return 0;
        def = rd_u32(light + 60);
        if (is_neg(def)) {
            const unsigned char* ld = take(c, 16);
            if (!ld || !xstr(c, rd_u32(ld)) || !image_ptr(c, rd_u32(ld + 4))) return 0;
        }
    }
    refl_n = rd_u32(b + 228);
    if (rd_u32(b + 232)) {
        const unsigned char* probes;
        if (refl_n > 256u) return 0;
        probes = refl_n ? take(c, refl_n * 16u) : NULL;
        if (refl_n && !probes) return 0;
        for (i = 0; i < refl_n; i++)
            if (!image_ptr(c, rd_u32(probes + i * 16u + 12u))) return 0;
    }
    if (rd_u32(b + 244) == FF_NEG1) {
        int pn = rd_i32(b + 8);
        const unsigned char* planes;
        if (pn < 0 || pn > 200000) return 0;
        planes = pn ? take(c, (unsigned)pn * 20u) : NULL;
        if (pn && !planes) return 0;
        w->planes = planes;
        w->plane_n = (unsigned)pn;
    }
    if (rd_u32(b + 248) && !skip_mul(c, (unsigned)rd_i32(b + 12), 2)) return 0;
    cell_n = (unsigned)rd_i32(b + 240);
    if (rd_u32(b + 260) && cell_n) {
        const unsigned char* cells;
        if (cell_n > 100000u) return 0;
        cells = take(c, cell_n * 56u);
        if (!cells) return 0;
        for (i = 0; i < cell_n; i++)
            if (!cell_body(c, cells + i * 56u, 0)) return 0;
    }
    lmap_n = (unsigned)rd_i32(b + 264);
    if (rd_u32(b + 268) && lmap_n) {
        const unsigned char* lmaps;
        if (lmap_n > 256u) return 0;
        lmaps = take(c, lmap_n * 8u);
        if (!lmaps) return 0;
        for (i = 0; i < lmap_n; i++) {
            if (!image_ptr(c, rd_u32(lmaps + i * 8u))) return 0;
            if (!image_ptr(c, rd_u32(lmaps + i * 8u + 4u))) return 0;
        }
    }
    grid = b + 272;
    if (rd_u32(grid + 28)) {
        unsigned axis = rd_u32(grid + 20);
        unsigned mn, mx, rows;
        if (axis > 2u) return 0;
        mn = rd_u16(grid + 8u + axis * 2u);
        mx = rd_u16(grid + 14u + axis * 2u);
        rows = mx >= mn ? mx - mn + 1u : 0u;
        if (rows > 100000u || !skip_mul(c, rows, 2)) return 0;
    }
    if (rd_u32(grid + 36) && !take(c, rd_u32(grid + 32))) return 0;
    if (rd_u32(grid + 44) && !skip_mul(c, rd_u32(grid + 40), 4)) return 0;
    if (rd_u32(grid + 52) && !skip_mul(c, rd_u32(grid + 48), 168)) return 0;
    model_n = (unsigned)rd_i32(b + 336);
    if (rd_u32(b + 340) && model_n) {
        if (model_n > 100000u || !skip_mul(c, model_n, 56)) return 0;
    }
    mem_n = (unsigned)rd_i32(b + 372);
    if (rd_u32(b + 376) && mem_n) {
        const unsigned char* mem;
        if (mem_n > 100000u) return 0;
        c->world_tracking=1;c->world_valid=1;
        world_alloc(c,4,mem_n*8u);
        mem = take(c, mem_n * 8u);
        if (!mem) return 0;
        w->world_mat_names=(char*)calloc(mem_n,64u);
        if(!w->world_mat_names)return 0;
        w->world_mat_n=mem_n;
        for (i = 0; i < mem_n; i++) {
            unsigned ptr=rd_u32(mem+i*8u);
            const char* name=NULL;
            if(is_neg(ptr)) {
                unsigned before=c->mat_n;
                if(ptr==FF_NEG2)world_alloc(c,4,4);
                if(!material(c))return 0;
                if(c->mat_n>before)name=c->mat_names+(c->mat_n-1u)*64u;
            } else if(ptr>1u && ((ptr-1u)>>28)==4u) {
                unsigned off=(ptr-1u)&0x0fffffffu;
                if(off>=c->asset_slot && (off-c->asset_slot)%8u==0) {
                    unsigned ai=(off-c->asset_slot)/8u;
                    if(ai<c->asset_n)name=c->asset_names+ai*64u;
                }
            }
            if(name)snprintf(w->world_mat_names+i*64u,64,"%s",name);
        }
        c->world_tracking=0;
    }
    if (rd_u32(b + 52)) {
        const unsigned char* vert = take(c, vert_count * 44u);
        if (!vert && vert_count) return 0;
        w->vert = vert;
        w->vert_n = vert_count;
    }
    if (rd_u32(b + 64) && !take(c, rd_u32(b + 60))) return 0;
    if (is_neg(rd_u32(b + 384)) && !material(c)) return 0;
    if (is_neg(rd_u32(b + 388)) && !material(c)) return 0;
    if (!image_ptr(c, rd_u32(b + 540))) return 0;
    if (rd_u32(b + 572)) {
        unsigned lights = rd_u32(b + 220);
        const unsigned char* sh;
        if (lights > 4096u) return 0;
        sh = lights ? take(c, lights * 12u) : NULL;
        if (lights && !sh) return 0;
        for (i = 0; i < lights; i++) {
            unsigned sc = rd_u16(sh + i * 12u);
            unsigned smc = rd_u16(sh + i * 12u + 2u);
            if (rd_u32(sh + i * 12u + 4u) && !skip_mul(c, sc, 2)) return 0;
            if (rd_u32(sh + i * 12u + 8u) && !skip_mul(c, smc, 2)) return 0;
        }
    }
    if (rd_u32(b + 576)) {
        unsigned lights = rd_u32(b + 220);
        const unsigned char* rg;
        if (lights > 4096u) return 0;
        rg = lights ? take(c, lights * 8u) : NULL;
        if (lights && !rg) return 0;
        for (i = 0; i < lights; i++) {
            unsigned hc = rd_u32(rg + i * 8u);
            unsigned hp = rd_u32(rg + i * 8u + 4u);
            const unsigned char* hulls;
            unsigned h;
            if (!hp) continue;
            if (hc > 256u) return 0;
            hulls = hc ? take(c, hc * 80u) : NULL;
            if (hc && !hulls) return 0;
            for (h = 0; h < hc; h++) {
                unsigned ac = rd_u32(hulls + h * 80u + 72u);
                unsigned ap = rd_u32(hulls + h * 80u + 76u);
                if (ap && !skip_mul(c, ac, 20)) return 0;
            }
        }
    }
    dpvs = b + 580;
    if (rd_u32(dpvs + 72)) {
        unsigned nsorted = rd_u32(dpvs + 4) + rd_u32(dpvs + 8);
        if (!skip_mul(c, nsorted, 2)) return 0;
    }
    if (rd_u32(dpvs + 76) && !skip_mul(c, rd_u32(dpvs), 28)) return 0;
    if (rd_u32(dpvs + 80) && surf_count) {
        const unsigned char* surfs = take(c, surf_count * 48u);
        if (!surfs) return 0;
        w->surf = surfs;
        w->surf_n = surf_count;
        w->mat_at_surfs = c->mat_n;
        w->surf_mat_lo = 0xffffffffu;
        for (i = 0; i < surf_count; i++) {
            unsigned p = rd_u32(surfs + i * 48u + 16u);
            unsigned o;
            if (is_neg(p)) {
                if (!material(c)) return 0;
                continue;
            }
            if (p <= 1u || ((p - 1u) >> 28) != 4u) continue;
            o = (p - 1u) & 0x0fffffffu;
            if (o < w->surf_mat_lo) w->surf_mat_lo = o;
            if (o > w->surf_mat_hi) w->surf_mat_hi = o;
            w->surf_mat_n++;
        }
        if (w->surf_mat_n && w->surf_mat_hi >= w->surf_mat_lo && ((w->surf_mat_hi - w->surf_mat_lo) % 8u) == 0)
            w->surf_mat_ok = 1;
        if(w->surf_mat_ok && (w->surf_mat_hi-w->surf_mat_lo)/8u+1u==w->world_mat_n)
            resolve_world_images(c,w->surf_mat_lo);
    }
    if (rd_u32(dpvs + 84)) {
        unsigned culls = (unsigned)rd_i32(b + 224);
        if (culls > 100000u || !skip_mul(c, culls, 32)) return 0;
    }
    if (rd_u32(dpvs + 88)) {
        unsigned models = rd_u32(dpvs);
        const unsigned char* draws;
        if (models > 100000u) return 0;
        draws = models ? take(c, models * 76u) : NULL;
        if (models && !draws) return 0;
        w->draws = draws;
        w->draw_n = models;
        for (i = 0; i < models; i++)
            if (is_neg(rd_u32(draws + i * 76u + 56u)) && !xmodel(c)) return 0;
    }
    return 1;
}

static int load_clip(Cur* c, WorldGeom* w) {
    const unsigned char* b = take(c, 284);
    unsigned i;
    if (!b || !xstr(c, rd_u32(b))) return 0;
    if (rd_u32(b + 12) == FF_NEG1) {
        int pn = rd_i32(b + 8);
        const unsigned char* planes;
        if (pn < 0 || pn > 200000) return 0;
        if (!w->planes) {
            planes = pn ? take(c, (unsigned)pn * 20u) : NULL;
            if (pn && !planes) return 0;
            w->planes = planes;
            w->plane_n = (unsigned)pn;
        } else if (pn && !skip_mul(c, (unsigned)pn, 20)) {
            return 0;
        }
    } else if (rd_u32(b + 12) > 1u) {
        unsigned v = rd_u32(b + 12) - 1u;
        if ((v >> 28) == 4u) {
            w->plane_base = v & 0x0fffffffu;
            w->planes_linked = 1;
        }
    }
    if (rd_u32(b + 20)) {
        unsigned n = rd_u32(b + 16);
        const unsigned char* sm;
        if (n > 100000u) return 0;
        sm = n ? take(c, n * 80u) : NULL;
        if (n && !sm) return 0;
        for (i = 0; i < n; i++)
            if (is_neg(rd_u32(sm + i * 80u + 4u)) && !xmodel(c)) return 0;
    }
    if (is_neg(rd_u32(b + 28))) {
        unsigned n = rd_u32(b + 24);
        const unsigned char* mats;
        if (n > 100000u) return 0;
        mats = n ? take(c, n * 72u) : NULL;
        if (n && !mats) return 0;
        w->cmats = mats;
        w->cmat_n = n;
    }
    if (rd_u32(b + 36)) {
        unsigned n = rd_u32(b + 32);
        const unsigned char* sides;
        if (n > 1000000u) return 0;
        sides = n ? take(c, n * 12u) : NULL;
        if (n && !sides) return 0;
        w->sides = sides;
        w->side_n = n;
        for (i = 0; i < n; i++) {
            const unsigned char* inline_plane;
            if (rd_u32(sides + i * 12u) != FF_NEG1) continue;
            inline_plane = take(c, 20);
            if (!inline_plane) return 0;
            if (!w->side_inline) {
                w->side_inline = (unsigned char*)calloc(n, 20u);
                if (!w->side_inline) return 0;
            }
            memcpy(w->side_inline + i * 20u, inline_plane, 20u);
        }
    }
    if (rd_u32(b + 44) && !take(c, rd_u32(b + 40))) return 0;
    if (rd_u32(b + 52)) {
        unsigned n = rd_u32(b + 48);
        const unsigned char* nodes;
        if (n > 1000000u) return 0;
        nodes = n ? take(c, n * 8u) : NULL;
        if (n && !nodes) return 0;
        for (i = 0; i < n; i++)
            if (rd_u32(nodes + i * 8u) == FF_NEG1 && !take(c, 20)) return 0;
    }
    if (rd_u32(b + 60) && !skip_mul(c, rd_u32(b + 56), 44)) return 0;
    if (rd_u32(b + 76) && !skip_mul(c, rd_u32(b + 72), 2)) return 0;
    if (rd_u32(b + 68)) {
        unsigned n = rd_u32(b + 64);
        const unsigned char* nodes;
        if (n > 1000000u) return 0;
        nodes = n ? take(c, n * 20u) : NULL;
        if (n && !nodes) return 0;
        for (i = 0; i < n; i++) {
            int lbc = (short)rd_u16(nodes + i * 20u + 2u);
            unsigned bp = rd_u32(nodes + i * 20u + 8u);
            if (lbc > 0 && is_neg(bp) && !skip_mul(c, (unsigned)lbc, 2)) return 0;
        }
    }
    if (rd_u32(b + 84) && !skip_mul(c, rd_u32(b + 80), 4)) return 0;
    if (rd_u32(b + 92)) {
        unsigned n = rd_u32(b + 88);
        const unsigned char* v;
        if (n > 1000000u) return 0;
        v = n ? take(c, n * 12u) : NULL;
        if (n && !v) return 0;
        w->cvert = v;
        w->cvert_n = n;
    }
    if (rd_u32(b + 100)) {
        int tc = rd_i32(b + 96);
        const unsigned char* idx;
        if (tc < 0 || tc > 1000000) return 0;
        idx = tc ? take(c, (unsigned)tc * 6u) : NULL;
        if (tc && !idx) return 0;
        w->cidx = idx;
        w->ctri_n = (unsigned)tc;
    }
    if (is_neg(rd_u32(b + 104))) {
        int tc = rd_i32(b + 96);
        unsigned bytes;
        if (tc < 0) return 0;
        bytes = (((unsigned)tc * 3u + 31u) / 32u) * 4u;
        if (bytes && !take(c, bytes)) return 0;
    }
    if (is_neg(rd_u32(b + 112))) {
        int n = rd_i32(b + 108);
        if (n < 0 || !skip_mul(c, (unsigned)n, 28)) return 0;
    }
    if (is_neg(rd_u32(b + 120))) {
        int n = rd_i32(b + 116);
        if (n < 0 || n > 1000000) return 0;
        w->partitions = take(c,(unsigned)n*12u);
        if(n && !w->partitions)return 0;
        w->partition_n=(unsigned)n;
    }
    if (is_neg(rd_u32(b + 128))) {
        int n = rd_i32(b + 124);
        if (n < 0 || n > 1000000) return 0;
        w->aabb_trees = take(c,(unsigned)n*32u);
        if(n && !w->aabb_trees)return 0;
        w->aabb_tree_n=(unsigned)n;
    }
    if (is_neg(rd_u32(b + 136)) && !skip_mul(c, rd_u32(b + 132), 72)) return 0;
    if (is_neg(rd_u32(b + 144))) {
        unsigned n = rd_u16(b + 140);
        const unsigned char* brushes;
        if (n > 100000u) return 0;
        brushes = n ? take(c, n * 80u) : NULL;
        if (n && !brushes) return 0;
        w->brushes = brushes;
        w->brush_n = n;
    }
    return 1;
}

static int walk_assets(const unsigned char* zone, unsigned zn, WorldGeom* w) {
    Cur c;
    unsigned strings, str_ptr, assets, asset_ptr, i, count;
    const unsigned char* hdr;
    unsigned* types = NULL;
    int saw = 0;
    memset(&c, 0, sizeof(c));
    g_vert_fill = 0;
    g_idx_fill = 0;
    g_vseg_n = 0;
    g_iseg_n = 0;
    c.d = zone;
    c.n = zn;
    if (zn < 60u) return 0;
    c.o = 44;
    strings = rd_u32(zone + 44);
    str_ptr = rd_u32(zone + 48);
    assets = rd_u32(zone + 52);
    asset_ptr = rd_u32(zone + 56);
    c.o = 60;
    if (strings > 100000u || assets > 100000u || !assets) return 0;
    if (is_neg(str_ptr)) {
        const unsigned char* ptrs = take(&c, strings * 4u);
        unsigned str_at, str_bytes;
        if (strings && !ptrs) return 0;
        str_at = c.o;
        for (i = 0; i < strings; i++)
            if (!xstr(&c, rd_u32(ptrs + i * 4u))) goto fail;
        str_bytes = c.o - str_at;
        c.asset_slot = ((strings * 4u + str_bytes + 3u) & ~3u) + 4u;
    }
    if (!is_neg(asset_ptr)) goto fail;
    hdr = take(&c, assets * 8u);
    if (!hdr) goto fail;
    types = (unsigned*)malloc(assets * sizeof(unsigned));
    if (!types) goto fail;
    for (i = 0; i < assets; i++) {
        unsigned ptr = rd_u32(hdr + i * 8u + 4u);
        if (!is_neg(ptr)) goto fail;
        types[i] = rd_u32(hdr + i * 8u);
    }
    g_inl_n = 0;
    g_mat_addr_n = 0;
    c.asset_n = assets;
    c.asset_names = (char*)calloc(assets, 64u);
    if (!c.asset_names) goto fail;
    count = assets;
    for (i = 0; i < count; i++) {
        unsigned t = types[i];
        c.asset_i = i;
        if (t == 16u) {
            if (!load_gfx(&c, w)) goto fail;
            if (i + 2 < count && types[i + 1] == 14u && types[i + 2] == 11u) {
                const unsigned char* gw = take(&c, 4);
                if (!gw || !xstr(&c, rd_u32(gw)) || !load_clip(&c, w)) goto fail;
            }
            saw = 1;
            break;
        }
        if (t == 2u) {
            if (!anim_parts(&c)) goto fail;
        } else if (t == 7u) {
            if (!sound_alias(&c)) goto fail;
        } else if (t == 23u) {
            if (!weapon_def(&c)) goto fail;
        } else if (t == 31u) {
            if (!raw_file(&c)) goto fail;
        } else if (t == 5u) {
            if (!techset(&c)) goto fail;
        } else if (t == 3u) {
            if (!xmodel(&c)) goto fail;
        } else if (t == 12u) {
            if (!comworld(&c)) goto fail;
        } else if (t == 17u) {
            if (!light_def(&c)) goto fail;
        } else if (t == 25u) {
            if (!fx_effect(&c)) goto fail;
        } else if (t == 4u) {
            unsigned before = c.mat_n;
            if (!material(&c)) goto fail;
            if (c.mat_n > before) memcpy(c.asset_names + i * 64u, c.mat_names + (c.mat_n - 1u) * 64u, 64u);
        } else {
            goto fail;
        }
    }
    w->colors = c.colors;
    w->color_n = c.color_n;
    w->mat_names = c.mat_names;
    w->mat_n = c.mat_n;
    w->props = c.props;
    w->prop_n = c.prop_n;
    w->asset_slot = c.asset_slot;
    w->asset_names = c.asset_names;
    w->asset_n = c.asset_n;
    free(c.world_allocs);
    free(types);
    return saw;
fail:
    if (c.props) {
        unsigned pi;
        for (pi = 0; pi < c.prop_n; pi++) {
            free(c.props[pi].surfs);
            free(c.props[pi].coll);
        }
    }
    free(c.world_allocs);
    free(c.props);
    free(c.asset_names);
    free(c.colors);
    free(c.mat_names);
    free(types);
    return 0;
}

static void find_ents(const unsigned char* zone, unsigned zn, WorldGeom* w) {
    static const char key[] = "\"classname\" \"worldspawn\"";
    unsigned kn = (unsigned)sizeof(key) - 1u;
    unsigned i, start, end;
    if (zn < kn) return;
    for (i = 0; i + kn <= zn; i++) {
        if (memcmp(zone + i, key, kn) == 0) break;
    }
    if (i + kn > zn) return;
    start = i;
    while (start && zone[start] != '{') start--;
    if (zone[start] != '{') return;
    end = start;
    while (end < zn && zone[end] != 0) end++;
    w->ents = zone + start;
    w->ent_n = end - start;
}

static int buf_add(Buf* b, const char* s, unsigned len) {
    if (b->n + len + 1 > b->cap) {
        unsigned cap = b->cap ? b->cap : 4096u;
        char* grown;
        while (cap < b->n + len + 1) {
            if (cap > 0x7fffffffu / 2u) return 0;
            cap *= 2u;
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

static void map_token(const char* name, char* out, int cap) {
    const char* base = name ? name : "";
    const char* s;
    int n = 0;
    for (s = base; *s; s++)
        if (*s == '/' || *s == '\\') base = s + 1;
    for (s = base; *s && *s != '.' && n + 1 < cap; s++) {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') out[n++] = ch;
    }
    if (!n && cap > 4) {
        memcpy(out, "cod4", 5);
        return;
    }
    if (cap) out[n] = 0;
}

/* The draw buffer also contains the sky shell. Those faces are large panels
   around the buildings. Without a sky shader they read as solid brushes, so
   the mesh keeps only faces near the floor spawns. */
typedef struct PlayVol {
    float mn[3];
    float mx[3];
    int use;
    float indoor[3];
    int has_indoor;
} PlayVol;

/* Helix keeps info_player_start unless a spawn_anchor names the deathmatch class.
   On these maps that single start sits in the sky volume; the floor points are
   mp_dm_spawn and mp_tdm_spawn. An indoor point replaces that list. */
static const char* deathmatch_anchor(const char* block, unsigned n) {
    const char* key = "\"classname\"";
    const char* p = block;
    const char* end = block + n;
    const char* q1;
    const char* q2;
    unsigned kn = 11;
    unsigned len;
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
    len = (unsigned)(q2 - q1);
    if (len == 11 && memcmp(q1, "mp_dm_spawn", 11) == 0) return "mp_deathmatch_spawn";
    if (len == 12 && memcmp(q1, "mp_tdm_spawn", 12) == 0) return "mp_teamdeathmatch_spawn";
    return 0;
}

static int block_origin(const char* block, float* o) {
    const char* origin = strstr(block, "\"origin\"");
    const char* q;
    if (!origin) return 0;
    q = strchr(origin + 8, '"');
    if (!q || sscanf(q + 1, "%f %f %f", &o[0], &o[1], &o[2]) != 3) return 0;
    return finite_f(o[0]) && finite_f(o[1]) && finite_f(o[2]);
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

static int append_entities(Buf* b, const unsigned char* data, unsigned ent_n, const PlayVol* play) {
    unsigned i = 0;
    while (i < ent_n && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n')) i++;
    if (i >= ent_n || data[i] != '{') return 1;
    while (i < ent_n) {
        unsigned start, depth, k, bn = 0;
        char* block;
        if (data[i] != '{') {
            char ch = (char)data[i];
            if ((ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 32 && ch < 127)) && !buf_add(b, &ch, 1)) return 0;
            i++;
            continue;
        }
        start = i;
        depth = 0;
        for (; i < ent_n; i++) {
            unsigned char ch = data[i];
            if (ch == '{') depth++;
            else if (ch == '}') {
                depth--;
                if (depth == 0) {
                    i++;
                    break;
                }
            }
        }
        block = (char*)malloc((size_t)(i - start) + 128u);
        if (!block) return 0;
        for (k = start; k < i; k++) {
            unsigned char ch = data[k];
            if (ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 32 && ch < 127)) block[bn++] = (char)ch;
        }
        block[bn] = 0;
        {
            const char* anchor = deathmatch_anchor(block, bn);
            unsigned cap = (unsigned)(i - start) + 128u;
            if (anchor && play && play->has_indoor) {
                float o[3];
                if (!block_origin(block, o) || fabsf(o[0] - play->indoor[0]) > 1.f || fabsf(o[1] - play->indoor[1]) > 1.f ||
                    fabsf(o[2] - play->indoor[2]) > 1.f)
                    anchor = 0;
                else
                    anchor = "mp_deathmatch_spawn";
            }
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
                            !buf_add(b, q2 + 1, (unsigned)(block + bn - (q2 + 1))) || !buf_str(b, "\n")) {
                            free(block);
                            return 0;
                        }
                        free(block);
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

static float half_f(unsigned short h) {
    unsigned sign = (unsigned)(h >> 15) & 1u;
    unsigned exp = (unsigned)(h >> 10) & 31u;
    unsigned mant = (unsigned)h & 1023u;
    unsigned bits;
    if (exp == 0u) {
        if (!mant) bits = sign << 31;
        else {
            exp = 127u - 14u;
            while ((mant & 1024u) == 0u) {
                mant <<= 1;
                exp--;
            }
            mant &= 1023u;
            bits = (sign << 31) | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31u)
        bits = (sign << 31) | (255u << 23) | (mant << 13);
    else
        bits = (sign << 31) | ((exp + (127u - 15u)) << 23) | (mant << 13);
    {
        float o;
        memcpy(&o, &bits, 4);
        return o;
    }
}

static void unpack_normal(const unsigned char* p, float* o) {
    float scale = ((float)p[3] + 192.f) / 32385.f;
    float x = ((float)p[0] - 127.f) * scale;
    float y = ((float)p[1] - 127.f) * scale;
    float z = ((float)p[2] - 127.f) * scale;
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 1.0e-4f) {
        o[0] = 0.f;
        o[1] = 0.f;
        o[2] = 1.f;
        return;
    }
    o[0] = x;
    o[1] = y;
    o[2] = z;
}

static int point_in_play(const PlayVol* v, float x, float y, float z) {
    return x >= v->mn[0] && x <= v->mx[0] && y >= v->mn[1] && y <= v->mx[1] && z >= v->mn[2] && z <= v->mx[2];
}

static int tri_in_play(const PlayVol* v, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy,
                       float cz) {
    float ab, bc, ac;
    if (!v->use) return 1;
    if (!point_in_play(v, ax, ay, az) || !point_in_play(v, bx, by, bz) || !point_in_play(v, cx, cy, cz)) return 0;
    ab = (ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz);
    bc = (bx - cx) * (bx - cx) + (by - cy) * (by - cy) + (bz - cz) * (bz - cz);
    ac = (ax - cx) * (ax - cx) + (ay - cy) * (ay - cy) + (az - cz) * (az - cz);
    return ab <= 480.f * 480.f && bc <= 480.f * 480.f && ac <= 480.f * 480.f;
}

static float ray_tri(const float* o, const float* d, float ax, float ay, float az, float bx, float by, float bz, float cx,
                     float cy, float cz) {
    float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    float e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    float px = d[1] * e2z - d[2] * e2y;
    float py = d[2] * e2x - d[0] * e2z;
    float pz = d[0] * e2y - d[1] * e2x;
    float det = e1x * px + e1y * py + e1z * pz;
    float inv, tx, ty, tz, u, qx, qy, qz, v, dist;
    if (det > -1.0e-6f && det < 1.0e-6f) return -1.f;
    inv = 1.f / det;
    tx = o[0] - ax;
    ty = o[1] - ay;
    tz = o[2] - az;
    u = (tx * px + ty * py + tz * pz) * inv;
    if (u < 0.f || u > 1.f) return -1.f;
    qx = ty * e1z - tz * e1y;
    qy = tz * e1x - tx * e1z;
    qz = tx * e1y - ty * e1x;
    v = (d[0] * qx + d[1] * qy + d[2] * qz) * inv;
    if (v < 0.f || u + v > 1.f) return -1.f;
    dist = (e2x * qx + e2y * qy + e2z * qz) * inv;
    return dist > 0.5f ? dist : -1.f;
}

static float closest_hit(const WorldGeom* w, const PlayVol* v, const float* o, const float* d) {
    float best = -1.f;
    unsigned i, t;
    for (i = 0; i < w->surf_n; i++) {
        const unsigned char* s = w->surf + i * 48u;
        int first = rd_i32(s + 4);
        unsigned vc = rd_u16(s + 8);
        unsigned tc = rd_u16(s + 10);
        int base = rd_i32(s + 12);
        if (first < 0 || base < 0 || !tc || !w->idx || !w->vert) continue;
        if ((unsigned)first > w->vert_n || vc > w->vert_n - (unsigned)first) continue;
        if ((unsigned)base > w->idx_n || tc > (w->idx_n - (unsigned)base) / 3u) continue;
        for (t = 0; t < tc; t++) {
            unsigned a = rd_u16(w->idx + ((unsigned)base + t * 3u) * 2u);
            unsigned bv = rd_u16(w->idx + ((unsigned)base + t * 3u + 1u) * 2u);
            unsigned cv = rd_u16(w->idx + ((unsigned)base + t * 3u + 2u) * 2u);
            const unsigned char *pa, *pb, *pc;
            float dist;
            if (a >= vc || bv >= vc || cv >= vc) continue;
            pa = w->vert + ((unsigned)first + a) * 44u;
            pb = w->vert + ((unsigned)first + bv) * 44u;
            pc = w->vert + ((unsigned)first + cv) * 44u;
            if (!tri_in_play(v, rd_f32(pa), rd_f32(pa + 4), rd_f32(pa + 8), rd_f32(pb), rd_f32(pb + 4), rd_f32(pb + 8),
                             rd_f32(pc), rd_f32(pc + 4), rd_f32(pc + 8)))
                continue;
            dist = ray_tri(o, d, rd_f32(pa), rd_f32(pa + 4), rd_f32(pa + 8), rd_f32(pb), rd_f32(pb + 4), rd_f32(pb + 8),
                           rd_f32(pc), rd_f32(pc + 4), rd_f32(pc + 8));
            if (dist > 0.f && (best < 0.f || dist < best)) best = dist;
        }
    }
    return best;
}

static void prepare_play(const WorldGeom* w, PlayVol* v) {
    float pts[128][3];
    int n = 0;
    unsigned i = 0;
    float best_up = 0.f;
    const unsigned char* data = w->ents;
    unsigned ent_n = w->ent_n;
    memset(v, 0, sizeof(*v));
    if (!data || !ent_n) return;
    while (i < ent_n) {
        unsigned start, depth;
        char block[2048];
        int bn;
        float o[3];
        if (data[i] != '{') {
            i++;
            continue;
        }
        start = i;
        depth = 0;
        for (; i < ent_n; i++) {
            if (data[i] == '{') depth++;
            else if (data[i] == '}') {
                depth--;
                if (depth == 0) {
                    i++;
                    break;
                }
            }
        }
        if (i - start >= sizeof(block)) continue;
        bn = 0;
        {
            unsigned k;
            for (k = start; k < i && bn + 1 < (int)sizeof(block); k++) {
                unsigned char ch = data[k];
                if (ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 32 && ch < 127)) block[bn++] = (char)ch;
            }
        }
        block[bn] = 0;
        if (!deathmatch_anchor(block, (unsigned)bn) || !block_origin(block, o) || o[2] > 160.f || n >= 128) continue;
        pts[n][0] = o[0];
        pts[n][1] = o[1];
        pts[n][2] = o[2];
        n++;
    }
    if (n < 1) return;
    v->mn[0] = v->mx[0] = pts[0][0];
    v->mn[1] = v->mx[1] = pts[0][1];
    v->mn[2] = v->mx[2] = pts[0][2];
    for (i = 1; i < (unsigned)n; i++) {
        int a;
        for (a = 0; a < 3; a++) {
            if (pts[i][a] < v->mn[a]) v->mn[a] = pts[i][a];
            if (pts[i][a] > v->mx[a]) v->mx[a] = pts[i][a];
        }
    }
    v->mn[0] -= 256.f;
    v->mx[0] += 256.f;
    v->mn[1] -= 256.f;
    v->mx[1] += 256.f;
    v->mn[2] -= 160.f;
    v->mx[2] += 280.f;
    v->use = 1;
    for (i = 0; i < (unsigned)n; i++) {
        float eye[3];
        float up, down, near, hit;
        static const float dirs[4][3] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}};
        int d;
        eye[0] = pts[i][0];
        eye[1] = pts[i][1];
        eye[2] = pts[i][2] + 40.f;
        up = closest_hit(w, v, eye, (float[3]){0.f, 0.f, 1.f});
        down = closest_hit(w, v, eye, (float[3]){0.f, 0.f, -1.f});
        if (up < 0.f || up > 160.f || down < 0.f || down > 120.f) continue;
        near = -1.f;
        for (d = 0; d < 4; d++) {
            hit = closest_hit(w, v, eye, dirs[d]);
            if (hit > 0.f && (near < 0.f || hit < near)) near = hit;
        }
        if (near < 0.f || near > 120.f) continue;
        if (!v->has_indoor || up < best_up) {
            v->indoor[0] = pts[i][0];
            v->indoor[1] = pts[i][1];
            v->indoor[2] = pts[i][2];
            v->has_indoor = 1;
            best_up = up;
        }
    }
}

static int huge_box(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = bx - ax, dy = by - ay, dz = bz - az;
    if (dx < 0.f) dx = -dx;
    if (dy < 0.f) dy = -dy;
    if (dz < 0.f) dz = -dz;
    return dx > 1000000.f || dy > 1000000.f || dz > 1000000.f;
}

static int emit_brush(Buf* b, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz) {
    float ux = bx - ax, uy = by - ay, uz = bz - az;
    float vx = cx - ax, vy = cy - ay, vz = cz - az;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    float ax2, ay2, az2, bx2, by2, bz2, cx2, cy2, cz2;
    char line[1200];
    int n;
    if (len < 1.0e-4f) return 1;
    if (huge_box(ax, ay, az, bx, by, bz) || huge_box(ax, ay, az, cx, cy, cz)) return 1;
    nx /= len;
    ny /= len;
    nz /= len;
    ax2 = ax - nx * 8.f;
    ay2 = ay - ny * 8.f;
    az2 = az - nz * 8.f;
    bx2 = bx - nx * 8.f;
    by2 = by - ny * 8.f;
    bz2 = bz - nz * 8.f;
    cx2 = cx - nx * 8.f;
    cy2 = cy - ny * 8.f;
    cz2 = cz - nz * 8.f;
    n = snprintf(line, sizeof(line),
                 "brush {\n"
                 "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                 "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                 "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                 "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                 "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) textures/dev/grid 0 0 0 1 1\n"
                 "visible false\ncontents solid\n}\n",
                 (double)ax, (double)ay, (double)az, (double)bx, (double)by, (double)bz, (double)cx, (double)cy, (double)cz,
                 (double)ax2, (double)ay2, (double)az2, (double)cx2, (double)cy2, (double)cz2, (double)bx2, (double)by2, (double)bz2,
                 (double)ax, (double)ay, (double)az, (double)ax2, (double)ay2, (double)az2, (double)bx, (double)by, (double)bz,
                 (double)bx, (double)by, (double)bz, (double)bx2, (double)by2, (double)bz2, (double)cx, (double)cy, (double)cz,
                 (double)cx, (double)cy, (double)cz, (double)cx2, (double)cy2, (double)cz2, (double)ax, (double)ay, (double)az);
    if (n <= 0 || n >= (int)sizeof(line)) return 0;
    return buf_add(b, line, (unsigned)n);
}

/* Preserve authored decal art. Asset names do not establish SP/MP ownership;
   visibility options such as the stock r_drawDecals are render policy. */
static int mat_usable(const char* name) {
    if (!name || !name[0]) return 0;
    if (strstr(name, "clip") || strstr(name, "caulk") || strstr(name, "nodraw") || strstr(name, "trigger") ||
        strstr(name, "sky") || strstr(name, "portal") || strstr(name, "hint") || strstr(name, "hotspot") ||
        strstr(name, "shadowcaster"))
        return 0;
    return 1;
}

static void clip_mat_name(const unsigned char* cmats, unsigned n, int idx, char* out, int cap) {
    const unsigned char* s;
    int i, o = 0;
    if (!out || cap < 2) return;
    out[0] = 0;
    if (!cmats || idx < 0 || (unsigned)idx >= n) return;
    s = cmats + (unsigned)idx * 72u;
    for (i = 0; i < 63 && s[i] && o + 1 < cap; i++) {
        unsigned char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/') out[o++] = (char)c;
    }
    out[o] = 0;
}

static const char* mat_tail(const char* s) {
    const char* slash;
    if (!s) return "";
    slash = strrchr(s, '/');
    return slash ? slash + 1 : s;
}

static int same_material_tail(const char* stored, const char* want) {
    const char* a = mat_tail(stored);
    const char* b = mat_tail(want);
    const char* decal;
    char alt[96];
    if (strcmp(a, b) == 0) return 1;
    if (strncmp(b, "com_", 4) == 0 && strcmp(a, b + 4) == 0) return 1;
    decal = strstr(b, "_decal");
    if (decal && decal[6] == 0) {
        size_t n = (size_t)(decal - b);
        if (n == strlen(a) && memcmp(a, b, n) == 0) return 1;
    }
    if (strncmp(b, "mil_decal_numberblack", 21) == 0) {
        snprintf(alt, sizeof(alt), "decal_number%s", b + 21);
        if (strcmp(a, alt) == 0) return 1;
    }
    return 0;
}

static const ColorMap* color_row(const ColorMap* colors, unsigned n, const char* mat) {
    unsigned i;
    if (!colors || !mat || !mat[0]) return NULL;
    for (i = 0; i < n; i++)
        if (strcmp(colors[i].mat, mat) == 0) return &colors[i];
    for (i = 0; i < n; i++)
        if (colors[i].image[0] && same_material_tail(colors[i].mat, mat)) return &colors[i];
    return NULL;
}

static const char* color_image(const ColorMap* colors, unsigned n, const char* mat) {
    const ColorMap* row = color_row(colors, n, mat);
    unsigned i;
    if (row && row->image[0]) return row->image;
    /* A referenced image can be shared by a separate material with its own
       render state. Reuse the image without replacing that material's state. */
    for(i=0;i<n;i++) if(colors[i].image[0] && same_material_tail(colors[i].mat,mat))return colors[i].image;
    return NULL;
}

/* The zone often stores only the shared image. com_ and the black number
   decal prefix are not part of the IWD file name. */
static const char* image_tail(const char* mat, char* tmp, int cap) {
    const char* tail = mat_tail(mat);
    if (strncmp(tail, "com_", 4) == 0) tail += 4;
    /* The green container roof color is stored with _ext_ in the IWD name. */
    if (strcmp(tail, "me_cargocontainergreen_roof") == 0) {
        snprintf(tmp, (size_t)cap, "me_cargocontainergreen_ext_roof");
        return tmp;
    }
    if (strncmp(tail, "mil_decal_numberblack", 21) == 0) {
        snprintf(tmp, (size_t)cap, "decal_number%s", tail + 21);
        return tmp;
    }
    return tail;
}

/* Axial slot order is minX, minY, minZ, maxX, maxY, maxZ. The color image is
   the material's own color map; images/<name>_col is only the fallback. */
static void image_for_mat(int idx, const unsigned char* cmats, unsigned cmat_n, const ColorMap* colors,
                          unsigned color_n, char* out, int cap) {
    int n;
    char name[64];
    const char* mapped;
    const char* fallback = "textures/dev/grid";
    if (!out || cap < 20) return;
    memcpy(out, fallback, strlen(fallback) + 1);
    clip_mat_name(cmats, cmat_n, idx, name, (int)sizeof(name));
    if (!mat_usable(name)) return;
    mapped = color_image(colors, color_n, name);
    if (mapped && mapped[0]) {
        if (!decorate_image(mapped, color_row(colors, color_n, name), out, cap))
            memcpy(out, fallback, strlen(fallback) + 1);
    } else {
        char tmp[96];
        n = snprintf(out, (size_t)cap, "images/%s_col", image_tail(name, tmp, (int)sizeof(tmp)));
        if (n <= 0 || n >= cap) memcpy(out, fallback, strlen(fallback) + 1);
    }
}

static void face_image(const unsigned char* br, int axis, const unsigned char* cmats, unsigned cmat_n,
                       const ColorMap* colors, unsigned color_n, char* out, int cap) {
    int idx = (int)(short)rd_u16(br + 36u + (unsigned)axis * 2u);
    image_for_mat(idx, cmats, cmat_n, colors, color_n, out, cap);
}

/* A unit plane through three points near the brush center. The host orients
   the winding. Corners on both sides means the plane actually cuts the box. */
static int read_plane(const unsigned char* p, float* nx, float* ny, float* nz, float* dist) {
    float len;
    *nx = rd_f32(p);
    *ny = rd_f32(p + 4);
    *nz = rd_f32(p + 8);
    *dist = rd_f32(p + 12);
    if (!finite_f(*nx) || !finite_f(*ny) || !finite_f(*nz) || !finite_f(*dist)) return 0;
    len = sqrtf((*nx) * (*nx) + (*ny) * (*ny) + (*nz) * (*nz));
    if (!(len > 1.0e-6f)) return 0;
    *nx /= len;
    *ny /= len;
    *nz /= len;
    *dist /= len;
    return 1;
}

static int plane_splits_box(float nx, float ny, float nz, float dist, float x0, float y0, float z0, float x1,
                            float y1, float z1) {
    float xs[2], ys[2], zs[2];
    int pos = 0, neg = 0, i, j, k;
    xs[0] = x0;
    xs[1] = x1;
    ys[0] = y0;
    ys[1] = y1;
    zs[0] = z0;
    zs[1] = z1;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 2; k++) {
                float d = nx * xs[i] + ny * ys[j] + nz * zs[k] - dist;
                if (d > 0.25f) pos++;
                else if (d < -0.25f) neg++;
            }
        }
    }
    return pos && neg;
}

typedef struct FacePt {
    float x, y, z;
} FacePt;

enum { kPolyMax = 48, kPlaneMax = 32 };

/* Keep the negative side (normal points out of the solid). A face is emitted
   from its clipped polygon, so the host's winding fix sees a point inside
   the real hull and does not turn a chamfer into the rest of the box. */
static int clip_poly(const FacePt* in, int n, float nx, float ny, float nz, float dist, FacePt* out) {
    int i, m = 0;
    const float eps = 0.01f;
    if (n < 1) return 0;
    for (i = 0; i < n; i++) {
        FacePt a = in[i];
        FacePt b = in[(i + 1) % n];
        float da = nx * a.x + ny * a.y + nz * a.z - dist;
        float db = nx * b.x + ny * b.y + nz * b.z - dist;
        int ain = da <= eps;
        int bin = db <= eps;
        if (ain && bin) {
            if (m < kPolyMax) out[m++] = b;
        } else if (ain != bin) {
            float denom = da - db;
            float t = fabsf(denom) > 1.0e-12f ? da / denom : 0.f;
            FacePt p;
            p.x = a.x + (b.x - a.x) * t;
            p.y = a.y + (b.y - a.y) * t;
            p.z = a.z + (b.z - a.z) * t;
            if (m < kPolyMax) out[m++] = p;
            if (!ain && bin && m < kPolyMax) out[m++] = b;
        }
    }
    return m;
}

static int plane_quad(float nx, float ny, float nz, float dist, float cx, float cy, float cz, float radius, FacePt* q) {
    float ux, uy, uz, ul, vx, vy, vz, nd, ox, oy, oz;
    static const float sx[4] = {-1.f, 1.f, 1.f, -1.f};
    static const float sy[4] = {-1.f, -1.f, 1.f, 1.f};
    int s;
    if (fabsf(nz) < 0.9f) {
        ux = ny;
        uy = -nx;
        uz = 0.f;
    } else {
        ux = 0.f;
        uy = nz;
        uz = -ny;
    }
    ul = sqrtf(ux * ux + uy * uy + uz * uz);
    if (!(ul > 1.0e-6f) || !(radius > 0.f)) return 0;
    ux /= ul;
    uy /= ul;
    uz /= ul;
    vx = ny * uz - nz * uy;
    vy = nz * ux - nx * uz;
    vz = nx * uy - ny * ux;
    nd = nx * cx + ny * cy + nz * cz - dist;
    ox = cx - nx * nd;
    oy = cy - ny * nd;
    oz = cz - nz * nd;
    for (s = 0; s < 4; s++) {
        q[s].x = ox + (ux * sx[s] + vx * sy[s]) * radius;
        q[s].y = oy + (uy * sx[s] + vy * sy[s]) * radius;
        q[s].z = oz + (uz * sx[s] + vz * sy[s]) * radius;
    }
    return 1;
}

static int clip_against(const float* pn, const float* pd, int np, int skip, const FacePt* seed, int seed_n, FacePt* out) {
    FacePt buf[2][kPolyMax];
    int n, i, side = 0;
    if (seed_n < 3 || seed_n > kPolyMax) return 0;
    memcpy(buf[0], seed, (unsigned)seed_n * sizeof(FacePt));
    n = seed_n;
    for (i = 0; i < np; i++) {
        if (i == skip) continue;
        n = clip_poly(buf[side], n, pn[i * 3], pn[i * 3 + 1], pn[i * 3 + 2], pd[i], buf[side ^ 1]);
        side ^= 1;
        if (n < 3) return 0;
    }
    memcpy(out, buf[side], (unsigned)n * sizeof(FacePt));
    return n;
}

static int poly_tri(const FacePt* p, int n, float nx, float ny, float nz, float* out) {
    int i;
    for (i = 1; i + 1 < n; i++) {
        float ax = p[i].x - p[0].x, ay = p[i].y - p[0].y, az = p[i].z - p[0].z;
        float bx = p[i + 1].x - p[0].x, by = p[i + 1].y - p[0].y, bz = p[i + 1].z - p[0].z;
        float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        float area = cx * nx + cy * ny + cz * nz;
        if (area > 0.25f || area < -0.25f) {
            int swap = area < 0.f;
            out[0] = p[0].x;
            out[1] = p[0].y;
            out[2] = p[0].z;
            out[3] = swap ? p[i + 1].x : p[i].x;
            out[4] = swap ? p[i + 1].y : p[i].y;
            out[5] = swap ? p[i + 1].z : p[i].z;
            out[6] = swap ? p[i].x : p[i + 1].x;
            out[7] = swap ? p[i].y : p[i + 1].y;
            out[8] = swap ? p[i].z : p[i + 1].z;
            return 1;
        }
    }
    return 0;
}

static int emit_face(Buf* b, const float* p, const char* mat) {
    char line[640];
    int n = snprintf(line, sizeof(line),
                     "( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) ( %.8g %.8g %.8g ) \"%s\" 0 0 0 1 1\n",
                     (double)p[0], (double)p[1], (double)p[2], (double)p[3], (double)p[4], (double)p[5],
                     (double)p[6], (double)p[7], (double)p[8], mat ? mat : "textures/dev/grid");
    return n > 0 && n < (int)sizeof(line) && buf_add(b, line, (unsigned)n);
}

static int side_base_of(const unsigned char* brushes, unsigned n, unsigned* base) {
    unsigned i, min_off = 0xffffffffu, max_off = 0, any = 0;
    for (i = 0; i < n; i++) {
        unsigned ns = rd_u32(brushes + i * 80u + 28);
        unsigned sp = rd_u32(brushes + i * 80u + 32);
        unsigned off;
        if (!ns || sp < 2u || sp == FF_NEG1 || sp == FF_NEG2) continue;
        if (((sp - 1u) >> 28) != 4u) continue;
        off = (sp - 1u) & 0x0fffffffu;
        if (off < min_off) min_off = off;
        if (off > max_off) max_off = off;
        any = 1;
    }
    if (!any || max_off < min_off || ((max_off - min_off) % 12u) != 0) return 0;
    *base = min_off;
    return 1;
}

/* Axial box plus each bevel, clipped so a face only keeps the solid side.
   Sky is not drawn. Player-clip remains invisible but blocks players. */
static int emit_clip_brushes(Buf* b, const WorldGeom* w, int hide) {
    const unsigned char* brushes = w->brushes;
    unsigned n = w->brush_n;
    unsigned side_base = 0;
    int have_sides = w->sides && w->side_n && w->planes && w->plane_n && w->planes_linked &&
                     side_base_of(brushes, n, &side_base);
    unsigned i, drawn = 0;
    for (i = 0; i < n; i++) {
        const unsigned char* br = brushes + i * 80u;
        float x0 = rd_f32(br), y0 = rd_f32(br + 4), z0 = rd_f32(br + 8);
        int contents = rd_i32(br + 12);
        float x1 = rd_f32(br + 16), y1 = rd_f32(br + 20), z1 = rd_f32(br + 24);
        float dx, dy, dz, cx, cy, cz, radius;
        float pn[kPlaneMax * 3];
        float pd[kPlaneMax];
        char pm[kPlaneMax][96];
        float pts[9];
        int np = 0, f, emitted = 0;
        unsigned ns, sp, idx, s;
        int ladder = 0;
        for (s = 0; s < 6u; s++) {
            char name[64];
            clip_mat_name(w->cmats, w->cmat_n, (short)rd_u16(br + 36u + s * 2u), name, sizeof(name));
            if (strcmp(mat_tail(name), "ladder") == 0) ladder = 1;
        }
        if (contents & 0x800) continue;
        if ((contents & 0x10011) == 0 && !ladder) continue;
        if (!finite_f(x0) || !finite_f(y0) || !finite_f(z0) || !finite_f(x1) || !finite_f(y1) || !finite_f(z1)) continue;
        dx = x1 - x0;
        dy = y1 - y0;
        dz = z1 - z0;
        if (dx < 0.f) dx = -dx;
        if (dy < 0.f) dy = -dy;
        if (dz < 0.f) dz = -dz;
        if (dx < 1.f || dy < 1.f || dz < 1.f || dx > 1000000.f || dy > 1000000.f || dz > 1000000.f) continue;
        cx = (x0 + x1) * 0.5f;
        cy = (y0 + y1) * 0.5f;
        cz = (z0 + z1) * 0.5f;
        radius = sqrtf(dx * dx + dy * dy + dz * dz);
        if (radius < 2.f) radius = 2.f;
        {
            static const float an[6][3] = {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            float ad[6];
            int a;
            ad[0] = -x0;
            ad[1] = -y0;
            ad[2] = -z0;
            ad[3] = x1;
            ad[4] = y1;
            ad[5] = z1;
            for (a = 0; a < 6; a++) {
                pn[np * 3] = an[a][0];
                pn[np * 3 + 1] = an[a][1];
                pn[np * 3 + 2] = an[a][2];
                pd[np] = ad[a];
                face_image(br, a, w->cmats, w->cmat_n, w->colors, w->color_n, pm[np], 96);
                np++;
            }
        }
        ns = rd_u32(br + 28);
        sp = rd_u32(br + 32);
        if (have_sides && ns && ns <= w->side_n && sp > 1u && ((sp - 1u) >> 28) == 4u) {
            unsigned off = (sp - 1u) & 0x0fffffffu;
            if (off >= side_base && ((off - side_base) % 12u) == 0) {
                idx = (off - side_base) / 12u;
                if (idx < w->side_n && idx + ns <= w->side_n) {
                    for (s = 0; s < ns && np < kPlaneMax; s++) {
                        const unsigned char* side = w->sides + (idx + s) * 12u;
                        unsigned pp = rd_u32(side);
                        unsigned mat = rd_u32(side + 4);
                        const unsigned char* raw = NULL;
                        float nx, ny, nz, dist;
                        if (pp == FF_NEG1) {
                            if (!w->side_inline) continue;
                            raw = w->side_inline + (idx + s) * 20u;
                        } else if (pp > 1u && ((pp - 1u) >> 28) == 4u) {
                            unsigned po = (pp - 1u) & 0x0fffffffu;
                            unsigned pi;
                            if (po < w->plane_base || ((po - w->plane_base) % 20u) != 0) continue;
                            pi = (po - w->plane_base) / 20u;
                            if (pi >= w->plane_n) continue;
                            raw = w->planes + pi * 20u;
                        }
                        if (!raw || !read_plane(raw, &nx, &ny, &nz, &dist)) continue;
                        if (!plane_splits_box(nx, ny, nz, dist, x0, y0, z0, x1, y1, z1)) continue;
                        pn[np * 3] = nx;
                        pn[np * 3 + 1] = ny;
                        pn[np * 3 + 2] = nz;
                        pd[np] = dist;
                        image_for_mat(mat <= 0x7fffffffu ? (int)mat : -1, w->cmats, w->cmat_n, w->colors, w->color_n, pm[np],
                                      96);
                        np++;
                    }
                }
            }
        }
        {
            float kept[kPlaneMax][9];
            int kept_i[kPlaneMax];
            for (f = 0; f < np; f++) {
                FacePt seed[4], clipped[kPolyMax];
                int vn;
                if (!plane_quad(pn[f * 3], pn[f * 3 + 1], pn[f * 3 + 2], pd[f], cx, cy, cz, radius, seed)) continue;
                vn = clip_against(pn, pd, np, f, seed, 4, clipped);
                if (vn < 3 || !poly_tri(clipped, vn, pn[f * 3], pn[f * 3 + 1], pn[f * 3 + 2], pts)) continue;
                memcpy(kept[emitted], pts, sizeof(pts));
                kept_i[emitted] = f;
                emitted++;
            }
            if (!buf_str(b, "brush {\n")) return 0;
            if (emitted >= 4) {
                for (f = 0; f < emitted; f++)
                    if (!emit_face(b, kept[f], pm[kept_i[f]])) return 0;
            } else {
                float box[6][9];
                int a;
                box[0][0] = x0; box[0][1] = y1; box[0][2] = z0; box[0][3] = x0; box[0][4] = y0; box[0][5] = z0; box[0][6] = x0; box[0][7] = y0; box[0][8] = z1;
                box[1][0] = x1; box[1][1] = y0; box[1][2] = z0; box[1][3] = x0; box[1][4] = y0; box[1][5] = z0; box[1][6] = x0; box[1][7] = y0; box[1][8] = z1;
                box[2][0] = x0; box[2][1] = y1; box[2][2] = z0; box[2][3] = x1; box[2][4] = y1; box[2][5] = z0; box[2][6] = x1; box[2][7] = y0; box[2][8] = z0;
                box[3][0] = x1; box[3][1] = y0; box[3][2] = z0; box[3][3] = x1; box[3][4] = y1; box[3][5] = z0; box[3][6] = x1; box[3][7] = y1; box[3][8] = z1;
                box[4][0] = x0; box[4][1] = y1; box[4][2] = z0; box[4][3] = x1; box[4][4] = y1; box[4][5] = z0; box[4][6] = x1; box[4][7] = y1; box[4][8] = z1;
                box[5][0] = x0; box[5][1] = y0; box[5][2] = z1; box[5][3] = x1; box[5][4] = y0; box[5][5] = z1; box[5][6] = x1; box[5][7] = y1; box[5][8] = z1;
                for (a = 0; a < 6; a++)
                    if (!emit_face(b, box[a], pm[a])) return 0;
            }
        }
        if (!buf_str(b, ladder ? "visible false\ncontents ladder\n}\n" :
                        ((contents & 0x10000) && !(contents & 0x11)) ? "visible false\ncontents playerclip\n}\n" :
                        (hide || !(contents & 1)) ? "visible false\ncontents solid\n}\n" : "contents solid\n}\n")) return 0;
        drawn++;
    }
    return drawn > 0;
}

/* Surface aliases address the MaterialMemory pointer slots. Preserve that
   table's order; later sun/flare materials must not shift its entries. */
static int surfaces_ready(const WorldGeom* w) {
    return w && w->vert && w->idx && w->surf && w->world_mat_names &&
        w->world_mat_n && w->surf_mat_ok &&
        (w->surf_mat_hi-w->surf_mat_lo)/8u+1u==w->world_mat_n;
}
static const char* surf_mat_name(const WorldGeom* w,unsigned ptr) {
    unsigned o,idx;
    const char* name;
    if(!surfaces_ready(w) || ptr<=1u || is_neg(ptr) || ((ptr-1u)>>28)!=4u)return NULL;
    o=(ptr-1u)&0x0fffffffu;
    if(o<w->surf_mat_lo || (o-w->surf_mat_lo)%8u)return NULL;
    idx=(o-w->surf_mat_lo)/8u;
    if(idx>=w->world_mat_n)return NULL;
    name=w->world_mat_names+idx*64u;
    return name[0]?name:NULL;
}

static int image_for_world_mat(const char* mat, const ColorMap* colors, unsigned color_n, char* out, int cap) {
    const char* mapped;
    int n;
    if (!out || cap < 20 || !mat_usable(mat)) return 0;
    { const ColorMap* row = color_row(colors, color_n, mat); if(row && row->camera_hidden)return 0; }
    mapped = color_image(colors, color_n, mat);
    if (mapped && mapped[0]) return decorate_image(mapped, color_row(colors, color_n, mat), out, cap);
    {
        char tmp[96];
        const char* tail = image_tail(mat, tmp, (int)sizeof(tmp));
        n = snprintf(out, (size_t)cap, "images/%s_col", tail);
    }
    return n > 0 && n < cap;
}

static int emit_draw_surfaces(Buf* b, const WorldGeom* w) {
    unsigned* remap;
    unsigned* src;
    unsigned i, drawn = 0;
    char image[256];
    char line[320];
    if (!surfaces_ready(w) || !w->vert_n) return 1;
    remap = (unsigned*)malloc(w->vert_n * sizeof(unsigned));
    src = (unsigned*)malloc(w->vert_n * sizeof(unsigned));
    if (!remap || !src) {
        free(remap);
        free(src);
        return 0;
    }
    memset(remap, 0xff, w->vert_n * sizeof(unsigned));
    for (i = 0; i < w->surf_n; i++) {
        const unsigned char* s = w->surf + i * 48u;
        int first = rd_i32(s + 4);
        unsigned vc = rd_u16(s + 8);
        unsigned tc = rd_u16(s + 10);
        int base = rd_i32(s + 12);
        const char* mat;
        float mn[3], mx[3];
        unsigned t, local = 0, k;
        if (first < 0 || base < 0 || !vc || !tc) continue;
        if ((unsigned)first > w->vert_n || vc > w->vert_n - (unsigned)first) continue;
        if ((unsigned)base > w->idx_n || tc > (w->idx_n - (unsigned)base) / 3u) continue;
        mn[0] = rd_f32(s + 24);
        mn[1] = rd_f32(s + 28);
        mn[2] = rd_f32(s + 32);
        mx[0] = rd_f32(s + 36);
        mx[1] = rd_f32(s + 40);
        mx[2] = rd_f32(s + 44);
        if (!finite_f(mn[0]) || !finite_f(mx[0]) || huge_box(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2])) continue;
        mat = surf_mat_name(w, rd_u32(s + 16));
        if (!image_for_world_mat(mat, w->colors, w->color_n, image, (int)sizeof(image))) continue;
        for (t = 0; t < tc; t++) {
            unsigned corner, tri[3];
            tri[0] = rd_u16(w->idx + ((unsigned)base + t * 3u) * 2u);
            tri[1] = rd_u16(w->idx + ((unsigned)base + t * 3u + 1u) * 2u);
            tri[2] = rd_u16(w->idx + ((unsigned)base + t * 3u + 2u) * 2u);
            if (tri[0] >= vc || tri[1] >= vc || tri[2] >= vc || tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
                continue;
            for (corner = 0; corner < 3u; corner++) {
                unsigned abs = (unsigned)first + tri[corner];
                if (abs >= w->vert_n) continue;
                if (remap[abs] != 0xffffffffu) continue;
                remap[abs] = local;
                src[local] = abs;
                local++;
            }
        }
        if (!local) continue;
        if (!buf_str(b, "mesh {\n  material \"") || !buf_str(b, image) || !buf_str(b, "\"\n  contents nonsolid\n")) {
            free(remap);
            free(src);
            return 0;
        }
        for (k = 0; k < local; k++) {
            const unsigned char* vert = w->vert + src[k] * 44u;
            float nrm[3];
            int n;
            unpack_normal(vert + 36, nrm);
            n = snprintf(line, sizeof(line), "  v %.8g %.8g %.8g %.8g %.8g %.8g %.8g %.8g\n", (double)rd_f32(vert),
                         (double)rd_f32(vert + 4), (double)rd_f32(vert + 8), (double)rd_f32(vert + 20),
                         (double)rd_f32(vert + 24), (double)nrm[0], (double)nrm[1], (double)nrm[2]);
            if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) {
                free(remap);
                free(src);
                return 0;
            }
        }
        for (t = 0; t < tc; t++) {
            unsigned a = rd_u16(w->idx + ((unsigned)base + t * 3u) * 2u);
            unsigned bv = rd_u16(w->idx + ((unsigned)base + t * 3u + 1u) * 2u);
            unsigned cv = rd_u16(w->idx + ((unsigned)base + t * 3u + 2u) * 2u);
            int n;
            if (a >= vc || bv >= vc || cv >= vc || a == bv || bv == cv || a == cv) continue;
            if ((unsigned)first + a >= w->vert_n || (unsigned)first + bv >= w->vert_n || (unsigned)first + cv >= w->vert_n)
                continue;
            n = snprintf(line, sizeof(line), "  f %u %u %u\n", remap[(unsigned)first + a], remap[(unsigned)first + bv],
                         remap[(unsigned)first + cv]);
            if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) {
                free(remap);
                free(src);
                return 0;
            }
        }
        for (k = 0; k < local; k++) remap[src[k]] = 0xffffffffu;
        if (!buf_str(b, "}\n")) {
            free(remap);
            free(src);
            return 0;
        }
        drawn++;
    }
    free(remap);
    free(src);
    (void)drawn;
    return 1;
}

static const PropModel* prop_by_asset(const WorldGeom* w, unsigned ptr) {
    unsigned o, idx, i;
    if (!w || !w->asset_slot || !w->props || ptr <= 1u || is_neg(ptr) || ((ptr - 1u) >> 28) != 4u) return NULL;
    o = (ptr - 1u) & 0x0fffffffu;
    if (o < w->asset_slot || ((o - w->asset_slot) % 8u) != 0) return NULL;
    idx = (o - w->asset_slot) / 8u;
    for (i = 0; i < w->prop_n; i++)
        if (w->props[i].asset == idx) return &w->props[i];
    return NULL;
}

static int emit_prop_image(const char* image, char* out, int cap) {
    if (!image || !image[0]) return snprintf(out, (size_t)cap, "textures/dev/grid");
    if (strncmp(image, "images/", 7) == 0 || strncmp(image, "textures/", 9) == 0) return snprintf(out, (size_t)cap, "%s", image);
    return snprintf(out, (size_t)cap, "images/%s", image);
}

/* Draw instances point at the asset record of the model. Lod0 vertices are
   model-space; the instance axis, origin and scale place them in the world. */
static int emit_static_models(Buf* b, const WorldGeom* w) {
    unsigned i;
    char image[256];
    char line[320];
    if (!w->draws || !w->draw_n || !w->props) return 1;
    for (i = 0; i < w->draw_n; i++) {
        const unsigned char* d = w->draws + i * 76u;
        const PropModel* model = prop_by_asset(w, rd_u32(d + 56));
        float ox, oy, oz, s, ax, ay, az, bx, by, bz, cx, cy, cz;
        unsigned srf;
        if (!model) continue;
        ox = rd_f32(d + 4);
        oy = rd_f32(d + 8);
        oz = rd_f32(d + 12);
        ax = rd_f32(d + 16);
        ay = rd_f32(d + 20);
        az = rd_f32(d + 24);
        bx = rd_f32(d + 28);
        by = rd_f32(d + 32);
        bz = rd_f32(d + 36);
        cx = rd_f32(d + 40);
        cy = rd_f32(d + 44);
        cz = rd_f32(d + 48);
        s = rd_f32(d + 52);
        if (!finite_f(ox) || !finite_f(oy) || !finite_f(oz) || !finite_f(s) || s < 0.001f || s > 64.f) continue;
        for (srf = model->lod_first; srf < model->lod_first + model->lod_count; srf++) {
            const PropSurf* surf = &model->surfs[srf];
            unsigned v, t;
            int n;
            if (surf->camera_hidden || !surf->vert || !surf->tri || !surf->vc || !surf->tc) continue;
            if (!finite_f(rd_f32(surf->vert)) || !finite_f(rd_f32(surf->vert + 4)) || !finite_f(rd_f32(surf->vert + 8)))
                continue;
            n = emit_prop_image(surf->image, image, (int)sizeof(image));
            if (n <= 0 || n >= (int)sizeof(image)) continue;
            if (!buf_str(b, "mesh {\n  material \"") || !buf_str(b, image) || !buf_str(b, "\"\n  contents nonsolid\n"))
                return 0;
            for (v = 0; v < surf->vc; v++) {
                const unsigned char* vert = surf->vert + v * 32u;
                float x = rd_f32(vert), y = rd_f32(vert + 4), z = rd_f32(vert + 8);
                float wx, wy, wz, nrm[3];
                if (!finite_f(x) || !finite_f(y) || !finite_f(z)) {
                    x = y = z = 0.f;
                }
                wx = ox + s * (x * ax + y * bx + z * cx);
                wy = oy + s * (x * ay + y * by + z * cy);
                wz = oz + s * (x * az + y * bz + z * cz);
                unpack_normal(vert + 24, nrm);
                {
                    float nx = nrm[0], ny = nrm[1], nz = nrm[2];
                    float len;
                    nrm[0] = nx * ax + ny * bx + nz * cx;
                    nrm[1] = nx * ay + ny * by + nz * cy;
                    nrm[2] = nx * az + ny * bz + nz * cz;
                    len = sqrtf(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]);
                    if (len > 1.e-6f) {
                        nrm[0] /= len; nrm[1] /= len; nrm[2] /= len;
                    }
                }
                n = snprintf(line, sizeof(line), "  v %.8g %.8g %.8g %.8g %.8g %.8g %.8g %.8g 0 0 %.8g %.8g %.8g %.8g\n", (double)wx, (double)wy,
                             (double)wz, (double)half_f((unsigned short)rd_u16(vert + 22)),
                             (double)half_f((unsigned short)rd_u16(vert + 20)), (double)nrm[0], (double)nrm[1],
                             (double)nrm[2], vert[16]/255.0, vert[17]/255.0, vert[18]/255.0, vert[19]/255.0);
                if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) return 0;
            }
            for (t = 0; t < surf->tc; t++) {
                unsigned a = rd_u16(surf->tri + t * 6u);
                unsigned bv = rd_u16(surf->tri + t * 6u + 2u);
                unsigned cv = rd_u16(surf->tri + t * 6u + 4u);
                if (a >= surf->vc || bv >= surf->vc || cv >= surf->vc || a == bv || bv == cv || a == cv) continue;
                n = snprintf(line, sizeof(line), "  f %u %u %u\n", a, cv, bv);
                if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) return 0;
            }
            if (!buf_str(b, "}\n")) return 0;
        }
        if (model->coll && model->coll_n) {
            unsigned t;
            if (!buf_str(b, "mesh {\n  material \"textures/dev/grid\"\n  visible 0\n  contents solid\n")) return 0;
            for (t = 0; t < model->coll_n; t++) {
                const float* tri = model->coll + t * 9u;
                int corner;
                float ux=tri[3]-tri[0], uy=tri[4]-tri[1], uz=tri[5]-tri[2];
                float vx=tri[6]-tri[0], vy=tri[7]-tri[1], vz=tri[8]-tri[2];
                float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
                float wxn=nx*ax+ny*bx+nz*cx, wyn=nx*ay+ny*by+nz*cy, wzn=nx*az+ny*bz+nz*cz;
                float len=sqrtf(wxn*wxn+wyn*wyn+wzn*wzn);
                if (len > 1.e-10f) { wxn/=len; wyn/=len; wzn/=len; }
                for (corner = 0; corner < 3; corner++) {
                    float x = tri[corner * 3], y = tri[corner * 3 + 1], z = tri[corner * 3 + 2];
                    float wx = ox + s * (x * ax + y * bx + z * cx);
                    float wy = oy + s * (x * ay + y * by + z * cy);
                    float wz = oz + s * (x * az + y * bz + z * cz);
                    int n = snprintf(line, sizeof(line), "  v %.8g %.8g %.8g 0 0 %.8g %.8g %.8g\n", (double)wx, (double)wy,
                                     (double)wz, (double)wxn, (double)wyn, (double)wzn);
                    if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) return 0;
                }
            }
            for (t = 0; t < model->coll_n; t++) {
                int n = snprintf(line, sizeof(line), "  f %u %u %u\n", t * 3u, t * 3u + 1u, t * 3u + 2u);
                if (n <= 0 || n >= (int)sizeof(line) || !buf_add(b, line, (unsigned)n)) return 0;
            }
            if (!buf_str(b, "}\n")) return 0;
        }
    }
    return 1;
}

/* Brushes do not cover collision patches/terrain. Leaf AABB trees select
   triangle partitions and their authored material contents. IW3 clip triangles
   use clockwise winding, so reverse them for Helix's outward collision normal. */
static int emit_clip_triangles(Buf* out,const WorldGeom* w) {
    unsigned char* solid;
    unsigned i,j,vcount=0;
    char line[256];
    if(!w->ctri_n || !w->partitions || !w->aabb_trees)return 1;
    if(!w->cidx || !w->cvert || !w->cmats)return 0;
    solid=(unsigned char*)calloc(w->ctri_n,1);if(!solid)return 0;
    for(i=0;i<w->aabb_tree_n;i++) {
        const unsigned char* tree=w->aabb_trees+i*32u;
        unsigned mat=rd_u16(tree+24),part=rd_u32(tree+28),first,n;
        const unsigned char* p;
        if(rd_u16(tree+26))continue;
        if(mat>=w->cmat_n || part>=w->partition_n)goto fail;
        if(!(rd_u32(w->cmats+mat*72u+68u)&0x10011u))continue;
        p=w->partitions+part*12u;first=rd_u32(p+4);n=p[0];
        if(first>w->ctri_n || n>w->ctri_n-first)goto fail;
        memset(solid+first,1,n);
    }
    if(!buf_str(out,"mesh {\n material \"textures/dev/grid\"\n visible 0\n contents solid\n"))goto fail;
    for(i=0;i<w->ctri_n;i++)if(solid[i]) {
        unsigned indices[3]={rd_u16(w->cidx+i*6u),rd_u16(w->cidx+i*6u+4u),rd_u16(w->cidx+i*6u+2u)};
        float xyz[9],nx,ny,nz,len,ux,uy,uz,vx,vy,vz;
        int count;
        for(j=0;j<3;j++) {
            const unsigned char* p;
            if(indices[j]>=w->cvert_n)goto fail;
            p=w->cvert+indices[j]*12u;
            xyz[j*3]=rd_f32(p);xyz[j*3+1]=rd_f32(p+4);xyz[j*3+2]=rd_f32(p+8);
            if(!finite_f(xyz[j*3]) || !finite_f(xyz[j*3+1]) || !finite_f(xyz[j*3+2]))goto fail;
        }
        ux=xyz[3]-xyz[0];uy=xyz[4]-xyz[1];uz=xyz[5]-xyz[2];
        vx=xyz[6]-xyz[0];vy=xyz[7]-xyz[1];vz=xyz[8]-xyz[2];
        nx=uy*vz-uz*vy;ny=uz*vx-ux*vz;nz=ux*vy-uy*vx;len=sqrtf(nx*nx+ny*ny+nz*nz);
        if(len<1.e-6f)continue;
        nx/=len;ny/=len;nz/=len;
        for(j=0;j<3;j++) {
            count=snprintf(line,sizeof(line)," v %.8g %.8g %.8g 0 0 %.8g %.8g %.8g\n",xyz[j*3],xyz[j*3+1],xyz[j*3+2],nx,ny,nz);
            if(count<=0 || count>=(int)sizeof(line) || !buf_add(out,line,(unsigned)count))goto fail;
        }
        count=snprintf(line,sizeof(line)," f %u %u %u\n",vcount,vcount+1u,vcount+2u);
        if(count<=0 || count>=(int)sizeof(line) || !buf_add(out,line,(unsigned)count))goto fail;
        vcount+=3u;
    }
    free(solid);return buf_str(out,"}\n");
fail:
    free(solid);return 0;
}

static int emit_map(const WorldGeom* w, const char* map_name, unsigned char** out_buf, unsigned int* out_size) {
    Buf buf;
    PlayVol play;
    char token[64];
    char line[160];
    unsigned i, t;
    int n;
    int from_brushes = w->brushes && w->brush_n;
    memset(&buf, 0, sizeof(buf));
    memset(&play, 0, sizeof(play));
    if (!from_brushes) prepare_play(w, &play);
    map_token(map_name, token, (int)sizeof(token));
    if (!buf_str(&buf, "map ") || !buf_str(&buf, token) || !buf_str(&buf, " {\nversion 2\n}\n")) goto fail;
    if (w->ents && !append_entities(&buf, w->ents, w->ent_n, from_brushes ? NULL : &play)) goto fail;
    if (from_brushes) {
        int skinned = surfaces_ready(w);
        if (!emit_clip_brushes(&buf, w, skinned) || !emit_clip_triangles(&buf,w)) goto fail;
        if (skinned && !emit_draw_surfaces(&buf, w)) goto fail;
        if (skinned && !emit_static_models(&buf, w)) goto fail;
        *out_buf = (unsigned char*)buf.p;
        *out_size = buf.n;
        return 1;
    }
    if (w->cvert && w->cidx) {
        for (t = 0; t < w->ctri_n; t++) {
            unsigned a = rd_u16(w->cidx + t * 6u);
            unsigned b = rd_u16(w->cidx + t * 6u + 2u);
            unsigned cv = rd_u16(w->cidx + t * 6u + 4u);
            const unsigned char *pa, *pb, *pc;
            if (a >= w->cvert_n || b >= w->cvert_n || cv >= w->cvert_n) continue;
            pa = w->cvert + a * 12u;
            pb = w->cvert + b * 12u;
            pc = w->cvert + cv * 12u;
            if (!tri_in_play(&play, rd_f32(pa), rd_f32(pa + 4), rd_f32(pa + 8), rd_f32(pb), rd_f32(pb + 4), rd_f32(pb + 8),
                             rd_f32(pc), rd_f32(pc + 4), rd_f32(pc + 8)))
                continue;
            if (!emit_brush(&buf, rd_f32(pa), rd_f32(pa + 4), rd_f32(pa + 8), rd_f32(pb), rd_f32(pb + 4), rd_f32(pb + 8),
                            rd_f32(pc), rd_f32(pc + 4), rd_f32(pc + 8)))
                goto fail;
        }
    }
    if (!w->vert || !w->idx || !w->surf) goto fail;
    if (!buf_str(&buf, "mesh {\n  material \"textures/dev/grid\"\n  contents nonsolid\n")) goto fail;
    for (i = 0; i < w->vert_n; i++) {
        const unsigned char* v = w->vert + i * 44u;
        float nrm[3];
        unpack_normal(v + 36, nrm);
        n = snprintf(line, sizeof(line), "  v %.8g %.8g %.8g %.8g %.8g %.8g %.8g %.8g\n", (double)rd_f32(v),
                     (double)rd_f32(v + 4), (double)rd_f32(v + 8), (double)rd_f32(v + 20), (double)rd_f32(v + 24),
                     (double)nrm[0], (double)nrm[1], (double)nrm[2]);
        if (n <= 0 || n >= (int)sizeof(line) || !buf_add(&buf, line, (unsigned)n)) goto fail;
    }
    for (i = 0; i < w->surf_n; i++) {
        const unsigned char* s = w->surf + i * 48u;
        int first = rd_i32(s + 4);
        unsigned vc = rd_u16(s + 8);
        unsigned tc = rd_u16(s + 10);
        int base = rd_i32(s + 12);
        float mn[3], mx[3];
        if (first < 0 || base < 0 || !tc) continue;
        if ((unsigned)first > w->vert_n || vc > w->vert_n - (unsigned)first) continue;
        if ((unsigned)base > w->idx_n || tc > (w->idx_n - (unsigned)base) / 3u) continue;
        mn[0] = rd_f32(s + 24);
        mn[1] = rd_f32(s + 28);
        mn[2] = rd_f32(s + 32);
        mx[0] = rd_f32(s + 36);
        mx[1] = rd_f32(s + 40);
        mx[2] = rd_f32(s + 44);
        if (!finite_f(mn[0]) || huge_box(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2])) continue;
        for (t = 0; t < tc; t++) {
            unsigned a = rd_u16(w->idx + ((unsigned)base + t * 3u) * 2u);
            unsigned bv = rd_u16(w->idx + ((unsigned)base + t * 3u + 1u) * 2u);
            unsigned cv = rd_u16(w->idx + ((unsigned)base + t * 3u + 2u) * 2u);
            if (a >= vc || bv >= vc || cv >= vc) continue;
            {
                const unsigned char* pa = w->vert + ((unsigned)first + a) * 44u;
                const unsigned char* pb = w->vert + ((unsigned)first + bv) * 44u;
                const unsigned char* pc = w->vert + ((unsigned)first + cv) * 44u;
                if (!tri_in_play(&play, rd_f32(pa), rd_f32(pa + 4), rd_f32(pa + 8), rd_f32(pb), rd_f32(pb + 4),
                                 rd_f32(pb + 8), rd_f32(pc), rd_f32(pc + 4), rd_f32(pc + 8)))
                    continue;
            }
            a += (unsigned)first;
            bv += (unsigned)first;
            cv += (unsigned)first;
            n = snprintf(line, sizeof(line), "  f %u %u %u\n", a + 1u, bv + 1u, cv + 1u);
            if (n <= 0 || n >= (int)sizeof(line) || !buf_add(&buf, line, (unsigned)n)) goto fail;
        }
    }
    if (!buf_str(&buf, "}\n")) goto fail;
    *out_buf = (unsigned char*)buf.p;
    *out_size = buf.n;
    return 1;
fail:
    free(buf.p);
    return 0;
}

static int inflate_zlib(const unsigned char* src, unsigned slen, unsigned char** out, unsigned* olen) {
    z_stream s;
    unsigned cap = 1024u * 1024u;
    unsigned char* buf;
    int r;
    memset(&s, 0, sizeof(s));
    if (inflateInit(&s) != Z_OK) return 0;
    buf = (unsigned char*)malloc(cap);
    if (!buf) {
        inflateEnd(&s);
        return 0;
    }
    s.next_in = (Bytef*)src;
    s.avail_in = slen;
    s.next_out = buf;
    s.avail_out = cap;
    for (;;) {
        r = inflate(&s, Z_NO_FLUSH);
        if (r == Z_STREAM_END) break;
        if (s.avail_out || (r != Z_OK && r != Z_BUF_ERROR)) {
            free(buf);
            inflateEnd(&s);
            return 0;
        }
        if (cap > FF_ZONE_MAX / 2u) {
            free(buf);
            inflateEnd(&s);
            return 0;
        }
        {
            unsigned char* grown = (unsigned char*)realloc(buf, cap * 2u);
            if (!grown) {
                free(buf);
                inflateEnd(&s);
                return 0;
            }
            buf = grown;
            s.next_out = buf + cap;
            s.avail_out = cap;
            cap *= 2u;
        }
    }
    inflateEnd(&s);
    *out = buf;
    *olen = (unsigned)s.total_out;
    return 1;
}

int cod4_ff_to_hxmap(const unsigned char* data, unsigned int size, const char* map_name, unsigned char** out_buf,
                     unsigned int* out_size) {
    unsigned char* zone = NULL;
    unsigned zn = 0;
    WorldGeom w;
    int ok;
    if (!data || !out_buf || !out_size) return 0;
    *out_buf = NULL;
    *out_size = 0;
    if (size < 16u || memcmp(data, "IWffu100", 8) != 0 || rd_u32(data + 8) != 5u) return 0;
    if (!inflate_zlib(data + 12, size - 12u, &zone, &zn) || zn < 64u) {
        free(zone);
        return 0;
    }
    memset(&w, 0, sizeof(w));
    ok = walk_assets(zone, zn, &w);
    if (ok) find_ents(zone, zn, &w);
    if (ok) ok = emit_map(&w, map_name, out_buf, out_size);
    if (w.props) {
        unsigned pi;
        for (pi = 0; pi < w.prop_n; pi++) {
            free(w.props[pi].surfs);
            free(w.props[pi].coll);
        }
    }
    free(w.props);
    free(w.asset_names);
    free(w.colors);
    free(w.mat_names);
    free(w.side_inline);
    free(w.world_mat_names);
    free(zone);
    return ok;
}

#include "cod4_ffui.inc"
