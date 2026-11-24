/*
 * Small CoD GSC stack-machine runner for map and gametype mains.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The compiler in compile.c lowers one GSC file into a GscProg. This file
 * executes that program against a GscHost and records the last integer
 * return of the entry function in gsc_vm_last_int. The literal
 * giveWeapon("name") scan from the original walker stays here for callers
 * that only want the weapon list. The legacy gsc_vm_exec runs thread once,
 * inline. The separate map-owned GscRuntime preserves coroutine stacks,
 * shared globals and entities across ticks; it is used by cod_sgame.
 */
#include "gsc/vm.h"
#include "gsc/linked.h"

#include "gsc/compile.h"
#include "gsc/op.h"

#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gsc_vm_last_int;
static int name_equal(const char* a,const char* b);

#define GSC_STACK_MAX 1024
#define GSC_FRAME_MAX 64
#define GSC_ARGUMENT_MAX 128
#define GSC_CALL_DEPTH_CAP 32

typedef enum GscVk {
    V_UNDEF = 0,
    V_INT,
    V_FLOAT,
    V_STR,
    V_ARRAY,
    V_OBJECT, V_FUNCTION, V_ISTR, V_VECTOR, V_ANIMATION, V_ANIMTREE
} GscVk;

typedef struct GscVal {
    GscVk kind;
    int i;
    double f;
    int s; /* string table index */
    void* p; /* GscArr* when V_ARRAY, GscObj* when V_OBJECT */
    double vector[3];
} GscVal;

typedef struct GscFrame {
    int return_ip;  /* ip to resume at in the caller; -1 for the entry frame */
    int nlocals;
    GscVal* locals; /* heap-allocated; freed on return */
    int from_thread; /* 1 if GSC_THREAD entered this frame */
    int sp_base;     /* caller stack depth after this frame's args were popped */
    GscVal saved_self;
} GscFrame;

typedef struct GscStringEntry {
    int key;
    GscVal value;
} GscStringEntry;

typedef struct GscArr {
    int arena_index;
    GscVal* items;
    int size;
    int cap;
    int count;
    GscStringEntry* strings;
    int nstrings, stringcap;
} GscArr;

typedef struct GscField {
    char key[64];
    GscVal val;
} GscField;

typedef struct GscObj {
    int arena_index;
    int is_entity;
    int retired;
    int deleted;
    int entity_id;
    GscField* fields;
    int size;
    int cap;
} GscObj;

/* Arena that owns every array and field object the run allocates. Freed
 * once at the end of exec_prog so values can be copied on the stack and
 * passed to callers without reference counting. */
typedef struct GscArena {
    void** items;
    int* kinds; /* 0 = GscArr, 1 = GscObj */
    int n;
    int cap;
    int limit; // Zero for the legacy one-shot runner.
} GscArena;

static GscVal make_undef(void) {
    GscVal v;
    memset(&v, 0, sizeof(v));
    v.kind = V_UNDEF;
    v.i = 0;
    v.f = 0.0;
    v.s = 0;
    v.p = NULL;
    return v;
}

static GscVal make_int(int x) {
    GscVal v = make_undef();
    v.kind = V_INT;
    v.i = x;
    v.f = 0.0;
    v.s = 0;
    v.p = NULL;
    return v;
}

static GscVal make_float(double x) {
    GscVal v = make_undef();
    v.kind = V_FLOAT;
    v.i = 0;
    v.f = x;
    v.s = 0;
    v.p = NULL;
    return v;
}

static int arena_record(GscArena* ar, void* obj, int kind) {
    if (ar->limit && ar->n >= ar->limit) return 0;
    if (ar->n >= ar->cap) {
        int nc = ar->cap ? ar->cap * 2 : 16;
        void** ni;
        int* nk;
        /* Commit each realloc into the arena before attempting the next.
         * If the second one fails, the arena still owns a consistent pair:
         * the first array is live at the new size, the second is live at
         * its old size, and arena_free walks `ar->n` entries only. The
         * next arena_record call will see the stale `ar->cap` and retry
         * the growth cleanly rather than double-freeing the first block. */
        ni = (void**)realloc(ar->items, (size_t)nc * sizeof(void*));
        if (!ni) return 0;
        ar->items = ni;
        nk = (int*)realloc(ar->kinds, (size_t)nc * sizeof(int));
        if (!nk) return 0;
        ar->kinds = nk;
        ar->cap = nc;
    }
    ar->items[ar->n] = obj;
    ar->kinds[ar->n] = kind;
    ar->n++;
    return 1;
}

static GscArr* arena_new_arr(GscArena* ar) {
    GscArr* a = (GscArr*)calloc(1, sizeof(GscArr));
    if (!a) return NULL;
    a->arena_index = ar->n;
    if (!arena_record(ar, a, 0)) {
        free(a);
        return NULL;
    }
    return a;
}

static GscObj* arena_new_obj(GscArena* ar) {
    GscObj* o = (GscObj*)calloc(1, sizeof(GscObj));
    if (!o) return NULL;
    o->arena_index = ar->n;
    if (!arena_record(ar, o, 1)) {
        free(o);
        return NULL;
    }
    return o;
}

static GscVal arena_array_value(GscArena* arena) {
    GscVal out = make_undef();
    out.p = arena_new_arr(arena);
    if (out.p) out.kind = V_ARRAY;
    return out;
}

static void arena_free(GscArena* ar) {
    int i;
    for (i = 0; i < ar->n; i++) {
        if (ar->kinds[i] == 0) {
            GscArr* a = (GscArr*)ar->items[i];
            if (a) {
                free(a->items);
                free(a->strings);
                free(a);
            }
        } else {
            GscObj* o = (GscObj*)ar->items[i];
            if (o) {
                free(o->fields);
                free(o);
            }
        }
    }
    free(ar->items);
    free(ar->kinds);
    ar->items = NULL;
    ar->kinds = NULL;
    ar->n = 0;
    ar->cap = 0;
}

/* Hard cap on the length of a GSC array. Anything out of range is a
 * silent no-op so a hostile or buggy script cannot drive the capacity
 * doubler past INT_MAX or allocate hundreds of megabytes. */
#define GSC_ARR_MAX 65536

static void arr_set(GscArr* a, int idx, GscVal v) {
    if (!a || idx < 0 || idx >= GSC_ARR_MAX) return;
    if (idx >= a->cap) {
        int nc = a->cap > 0 ? a->cap : 8;
        GscVal* ni;
        /* idx is already < GSC_ARR_MAX. Double only while the product
         * fits in a signed int; never do nc *= 2 once nc > INT_MAX/2
         * or past the hard cap. */
        while (nc <= idx) {
            if (nc > GSC_ARR_MAX / 2) {
                nc = GSC_ARR_MAX;
                break;
            }
            nc *= 2;
        }
        if (nc > GSC_ARR_MAX) nc = GSC_ARR_MAX;
        ni = (GscVal*)realloc(a->items, (size_t)nc * sizeof(GscVal));
        if (!ni) return;
        a->items = ni;
        a->cap = nc;
    }
    while (a->size <= idx) {
        a->items[a->size++] = make_undef();
    }
    if (a->items[idx].kind == V_UNDEF && v.kind != V_UNDEF) a->count++;
    if (a->items[idx].kind != V_UNDEF && v.kind == V_UNDEF) a->count--;
    a->items[idx] = v;
}

static GscVal arr_get_string(const GscArr* a, int key) {
    int i;
    if (a) for (i = 0; i < a->nstrings; i++)
        if (a->strings[i].key == key) return a->strings[i].value;
    return make_undef();
}

static int arr_set_string(GscArr* a, int key, GscVal value) {
    int i;
    if (!a) return 0;
    for (i = 0; i < a->nstrings; i++) if (a->strings[i].key == key) {
        if (value.kind == V_UNDEF) a->strings[i] = a->strings[--a->nstrings];
        else a->strings[i].value = value;
        return 1;
    }
    if (value.kind == V_UNDEF) return 1;
    if (a->nstrings >= GSC_ARR_MAX) return 0;
    if (a->nstrings >= a->stringcap) {
        int cap = a->stringcap ? a->stringcap * 2 : 8;
        GscStringEntry* entries = (GscStringEntry*)realloc(a->strings, (size_t)cap * sizeof(*entries));
        if (!entries) return 0;
        a->strings = entries; a->stringcap = cap;
    }
    a->strings[a->nstrings].key = key;
    a->strings[a->nstrings++].value = value;
    return 1;
}

static int obj_find(const GscObj* o, const char* key) {
    int i;
    if (!o || !key) return -1;
    for (i = 0; i < o->size; i++) {
        if (name_equal(o->fields[i].key, key)) return i;
    }
    return -1;
}

static void obj_set(GscObj* o, const char* key, GscVal v) {
    int idx;
    if (!o || !key) return;
    idx = obj_find(o, key);
    if (idx >= 0) {
        o->fields[idx].val = v;
        return;
    }
    if (o->size >= o->cap) {
        int nc = o->cap ? o->cap * 2 : 4;
        GscField* nf = (GscField*)realloc(o->fields, (size_t)nc * sizeof(GscField));
        if (!nf) return;
        o->fields = nf;
        o->cap = nc;
    }
    snprintf(o->fields[o->size].key, sizeof(o->fields[o->size].key), "%s", key);
    o->fields[o->size].val = v;
    o->size++;
}

static const char* str_at(const GscProg* prog, int idx) {
    if (idx < 0 || idx >= prog->nstr) return "";
    return prog->strs + prog->stroff[idx];
}

/* Appends s to prog's string table, or returns the existing index.
 * The compiler's intern lives on Comp; a far call needs the same thing
 * so a string argument or return can cross into the other program. */
static uint32_t string_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
static int prog_intern(GscProg *p, const char *s) {
    size_t len;
    unsigned at;
    int id, alias = -1;
    if (!p || !s)
        return -1;
    len = strlen(s);
    if (!p->hashcap || (p->nstr + 1) * 2 >= p->hashcap) {
        int cap = p->hashcap ? p->hashcap * 2 : 256;
        int *hash;
        while (cap < (p->nstr + 1) * 2)
            cap *= 2;
        hash = (int *)calloc((size_t)cap, sizeof(int));
        if (!hash)
            return -1;
        for (id = 0; id < p->nstr; id++) {
            unsigned k = string_hash(str_at(p, id)) & (unsigned)(cap - 1);
            while (hash[k])
                k = (k + 1) & (unsigned)(cap - 1);
            hash[k] = id + 1;
        }
        free(p->strhash);
        p->strhash = hash;
        p->hashcap = cap;
    }
    at = string_hash(s) & (unsigned)(p->hashcap - 1);
    while (p->strhash[at]) {
        id = p->strhash[at] - 1;
        if (!strcmp(str_at(p, id), s))
            return id;
        at = (at + 1) & (unsigned)(p->hashcap - 1);
    }
    if (!p->strbytes && p->nstr)
        p->strbytes = p->stroff[p->nstr - 1] + (int)strlen(str_at(p, p->nstr - 1)) + 1;
    if (p->nstr >= 262144 || len > 65536 || p->strbytes > 64 * 1024 * 1024 - (int)len - 1)
        return -1;
    if (p->strs && (uintptr_t)s >= (uintptr_t)p->strs &&
        (uintptr_t)s < (uintptr_t)p->strs + (unsigned)p->strbytes)
        alias = (int)((uintptr_t)s - (uintptr_t)p->strs);
    if (p->nstr >= p->scap) {
        int cap = p->scap ? p->scap * 2 : 128;
        int *offsets = (int *)realloc(p->stroff, (size_t)cap * sizeof(int));
        if (!offsets)
            return -1;
        p->stroff = offsets;
        p->scap = cap;
    }
    if (p->strbytes + (int)len + 1 > p->bytecap) {
        int cap = p->bytecap ? p->bytecap : 1024;
        char *bytes;
        while (cap < p->strbytes + (int)len + 1)
            cap *= 2;
        bytes = (char *)realloc(p->strs, (size_t)cap);
        if (!bytes)
            return -1;
        p->strs = bytes;
        p->bytecap = cap;
    }
    if (alias >= 0)
        s = p->strs + alias;
    id = p->nstr++;
    p->stroff[id] = p->strbytes;
    memcpy(p->strs + p->strbytes, s, len + 1);
    p->strbytes += (int)len + 1;
    p->strhash[at] = id + 1;
    return id;
}

static int name_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}

static int val_truthy(const GscVal* v) {
    if (v->kind == V_INT) return v->i != 0;
    if (v->kind == V_FLOAT) return v->f != 0.0;
    if (v->kind == V_STR || v->kind == V_ISTR || v->kind == V_FUNCTION) return 1;
    if (v->kind == V_OBJECT || v->kind == V_ARRAY) return v->p != NULL;
    return 0;
}

static int val_eq(const GscVal* a, const GscVal* b) {
    if ((a->kind == V_ANIMATION || a->kind == V_ANIMTREE) && a->kind == b->kind)
        return a->i == b->i && a->s == b->s;
    if (a->kind == V_VECTOR && b->kind == V_VECTOR)
        return a->vector[0] == b->vector[0] && a->vector[1] == b->vector[1] && a->vector[2] == b->vector[2];
    if ((a->kind == V_OBJECT || a->kind == V_ARRAY) && a->kind == b->kind) return a->p == b->p;
    if (a->kind == V_FUNCTION && b->kind == V_FUNCTION) return a->i == b->i;
    if (a->kind == V_INT && b->kind == V_INT) return a->i == b->i;
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return da == db;
    }
    if ((a->kind == V_STR || a->kind == V_ISTR) && a->kind == b->kind) return a->s == b->s;
    if (a->kind == V_UNDEF && b->kind == V_UNDEF) return 1;
    return 0;
}

static int val_lt(const GscVal* a, const GscVal* b) {
    if (a->kind == V_INT && b->kind == V_INT) return a->i < b->i;
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return da < db;
    }
    return 0;
}

static int val_gt(const GscVal* a, const GscVal* b) {
    if (a->kind == V_INT && b->kind == V_INT) return a->i > b->i;
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return da > db;
    }
    return 0;
}

static GscVal val_add(const GscVal* a, const GscVal* b, GscProg* prog) {
    if (a->kind == V_VECTOR && b->kind == V_VECTOR) {
        GscVal out = *a; int i;
        for (i = 0; i < 3; i++) out.vector[i] += b->vector[i];
        return out;
    }
    if (a->kind == V_STR || b->kind == V_STR || a->kind == V_ISTR || b->kind == V_ISTR) {
        const GscVal* values[2] = {a, b};
        const char* parts[2];
        char numbers[2][48];
        char* joined;
        size_t lengths[2];
        int i, index;
        GscVal result = make_undef();
        for (i = 0; i < 2; i++) {
            if (values[i]->kind == V_STR || values[i]->kind == V_ISTR) parts[i] = str_at(prog, values[i]->s);
            else if (values[i]->kind == V_INT) {
                snprintf(numbers[i], sizeof(numbers[i]), "%d", values[i]->i);
                parts[i] = numbers[i];
            } else if (values[i]->kind == V_FLOAT) {
                snprintf(numbers[i], sizeof(numbers[i]), "%.9g", values[i]->f);
                parts[i] = numbers[i];
            } else return result;
            lengths[i] = strlen(parts[i]);
        }
        if (lengths[0] > 65536 || lengths[1] > 65536 - lengths[0]) return result;
        joined = (char*)malloc(lengths[0] + lengths[1] + 5);
        if (!joined) return result;
        {size_t offset=0;for(i=0;i<2;i++){
            if(values[i]->kind==V_ISTR)joined[offset++]='\x1e';
            memcpy(joined+offset,parts[i],lengths[i]);offset+=lengths[i];
            if(values[i]->kind==V_ISTR)joined[offset++]='\x1e';
        }joined[offset]=0;}
        index = prog_intern(prog, joined);
        free(joined);
        if (index >= 0) { result.kind = V_STR; result.s = index; }
        return result;
    }
    if (a->kind == V_INT && b->kind == V_INT)
        return make_int((int)((uint32_t)a->i + (uint32_t)b->i));
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return make_float(da + db);
    }
    return make_undef();
}

static GscVal val_neg(const GscVal* value) {
    GscVal out = *value;
    int i;
    if (out.kind == V_INT) out.i = (int)(0u - (uint32_t)out.i);
    else if (out.kind == V_FLOAT) out.f = -out.f;
    else if (out.kind == V_VECTOR)
        for (i = 0; i < 3; i++) out.vector[i] = -out.vector[i];
    else out = make_undef();
    return out;
}

static GscVal val_sub(const GscVal* a, const GscVal* b) {
    if (a->kind == V_VECTOR && b->kind == V_VECTOR) {
        GscVal out = *a; int i;
        for (i = 0; i < 3; i++) out.vector[i] -= b->vector[i];
        return out;
    }
    if (a->kind == V_INT && b->kind == V_INT)
        return make_int((int)((uint32_t)a->i - (uint32_t)b->i));
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return make_float(da - db);
    }
    return make_undef();
}

static GscVal val_mul(const GscVal* a, const GscVal* b) {
    if ((a->kind == V_VECTOR && (b->kind == V_INT || b->kind == V_FLOAT)) ||
        (b->kind == V_VECTOR && (a->kind == V_INT || a->kind == V_FLOAT))) {
        const GscVal* scalar = a->kind == V_VECTOR ? b : a;
        GscVal out = a->kind == V_VECTOR ? *a : *b;
        double scale = scalar->kind == V_INT ? scalar->i : scalar->f;
        int i; for (i = 0; i < 3; i++) out.vector[i] *= scale;
        return out;
    }
    if (a->kind == V_INT && b->kind == V_INT)
        return make_int((int)((uint32_t)a->i * (uint32_t)b->i));
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return make_float(da * db);
    }
    return make_undef();
}

static GscVal val_div(const GscVal* a, const GscVal* b) {
    if (a->kind == V_VECTOR && (b->kind == V_INT || b->kind == V_FLOAT)) {
        GscVal out = *a;
        double scale = b->kind == V_INT ? b->i : b->f;
        int i; if (scale == 0.0) return make_undef();
        for (i = 0; i < 3; i++) out.vector[i] /= scale;
        return out;
    }
    if (a->kind == V_INT && b->kind == V_INT) {
        if (b->i == 0) return make_undef();
        /* Signed INT_MIN / -1 is undefined behaviour in C (SIGFPE on x86).
         * Keep the low 32 bits (INT_MIN) to match the wrap the rest of the
         * machine uses for int-int arithmetic. */
        if (a->i == INT_MIN && b->i == -1) return make_int(INT_MIN);
        return make_int(a->i / b->i);
    }
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        if (db == 0.0) return make_undef();
        return make_float(da / db);
    }
    return make_undef();
}

/* Fires one host callback. argc arguments live at args[0..argc-1]; the first
 * string argument, if any, is the one the host wants. GSC_BU_ALLIES and
 * GSC_BU_AXIS are handled by the caller because they need shared state. */
static void builtin_call(GscHost* host, int id, const GscVal* args, int argc,
                         const GscProg* prog) {
    if (!host) return;
    switch (id) {
    case GSC_BU_GIVE:
        if (host->give_weapon && argc > 0 && args[0].kind == V_STR) {
            const char* s = str_at(prog, args[0].s);
            if (s && s[0]) host->give_weapon(host->user, s);
        }
        break;
    case GSC_BU_TAKE:
        if (host->take_weapons) host->take_weapons(host->user);
        break;
    case GSC_BU_AMBIENT:
        if (host->ambient && argc > 0 && args[0].kind == V_STR) {
            const char* s = str_at(prog, args[0].s);
            if (s && s[0]) host->ambient(host->user, s);
        }
        break;
    case GSC_BU_PC_MODEL:
    case GSC_BU_PC_SOUND:
    case GSC_BU_PC_SHADER:
    case GSC_BU_PC_WEAPON:
    case GSC_BU_LOAD_FX:
        if (host->precache && argc > 0 && args[0].kind == V_STR) {
            const char* name = str_at(prog, args[0].s);
            int kind = id == GSC_BU_PC_MODEL ? GSC_PC_MODEL : id == GSC_BU_PC_SOUND ? GSC_PC_SOUND :
                id == GSC_BU_PC_SHADER ? GSC_PC_SHADER : id == GSC_BU_PC_WEAPON ? GSC_PC_WEAPON : GSC_PC_FX;
            if (name && name[0]) host->precache(host->user, kind, name);
        }
        break;
    case GSC_BU_SET_CVAR:
        if (host->set_cvar && argc >= 2 && args[0].kind == V_STR) {
            char value[64];const char* text=NULL;
            if(args[1].kind==V_STR)text=str_at(prog,args[1].s);
            else if(args[1].kind==V_INT){snprintf(value,sizeof(value),"%d",args[1].i);text=value;}
            else if(args[1].kind==V_FLOAT){snprintf(value,sizeof(value),"%.9g",args[1].f);text=value;}
            if(text)host->set_cvar(host->user,str_at(prog,args[0].s),text);
        }
        break;
    case GSC_BU_GAMETYPE:
        if (host->set_gametype && argc > 0 && args[0].kind == V_STR) {
            const char* s = str_at(prog, args[0].s);
            if (s && s[0]) host->set_gametype(host->user, s);
        }
        break;
    default:
        break;
    }
}

/* Identity of an array or field object already copied into the caller
 * arena, so a cycle (a[0] = a) does not recurse forever. */
typedef struct GscSeen {
    const void* src;
    GscVal dst;
} GscSeen;

static int seen_find(const GscSeen* seen, int n, const void* src, GscVal* out) {
    int i;
    for (i = 0; i < n; i++) {
        if (seen[i].src == src) {
            *out = seen[i].dst;
            return 1;
        }
    }
    return 0;
}

static int seen_add(GscSeen** seen, int* n, int* cap, const void* src, GscVal dst) {
    if (*n >= *cap) {
        int nc = *cap ? *cap * 2 : 8;
        GscSeen* ns = (GscSeen*)realloc(*seen, (size_t)nc * sizeof(GscSeen));
        if (!ns) return 0;
        *seen = ns;
        *cap = nc;
    }
    (*seen)[*n].src = src;
    (*seen)[*n].dst = dst;
    (*n)++;
    return 1;
}

static GscVal copy_val_rec(const GscVal* v, const GscProg* src, GscProg* dst,
                           GscArena* arena, GscSeen** seen, int* nseen, int* scap) {
    GscVal out;
    if (v->kind == V_INT || v->kind == V_FLOAT || v->kind == V_UNDEF || v->kind == V_VECTOR)
        return *v;
    if (v->kind == V_STR || v->kind == V_ISTR) {
        int ni = prog_intern(dst, str_at(src, v->s));
        out = make_undef();
        if (ni < 0) return out;
        out.kind = v->kind;
        out.s = ni;
        return out;
    }
    if (v->kind == V_ARRAY) {
        GscArr* sa;
        GscArr* na;
        int i;
        if (!v->p) return make_undef();
        if (seen_find(*seen, *nseen, v->p, &out)) return out;
        na = arena_new_arr(arena);
        if (!na) return make_undef();
        out = make_undef();
        out.kind = V_ARRAY;
        out.p = na;
        if (!seen_add(seen, nseen, scap, v->p, out)) return make_undef();
        sa = (GscArr*)v->p;
        if (sa->size > 0) {
            na->items = (GscVal*)calloc((size_t)sa->size, sizeof(GscVal));
            if (!na->items) return out;
            na->cap = sa->size;
            na->size = sa->size;
            na->count = sa->count;
            for (i = 0; i < sa->size; i++)
                na->items[i] = copy_val_rec(&sa->items[i], src, dst, arena,
                                            seen, nseen, scap);
        }
        for (i = 0; i < sa->nstrings; i++) {
            int key = prog_intern(dst, str_at(src, sa->strings[i].key));
            GscVal value = copy_val_rec(&sa->strings[i].value, src, dst, arena, seen, nseen, scap);
            if (key < 0 || !arr_set_string(na, key, value)) return make_undef();
        }
        return out;
    }
    if (v->kind == V_OBJECT) {
        GscObj* so;
        GscObj* no;
        int i;
        if (!v->p) return make_undef();
        if (seen_find(*seen, *nseen, v->p, &out)) return out;
        no = arena_new_obj(arena);
        if (!no) return make_undef();
        out = make_undef();
        out.kind = V_OBJECT;
        out.p = no;
        if (!seen_add(seen, nseen, scap, v->p, out)) return make_undef();
        so = (GscObj*)v->p;
        if (so->size > 0) {
            no->fields = (GscField*)calloc((size_t)so->size, sizeof(GscField));
            if (!no->fields) return out;
            no->cap = so->size;
            no->size = so->size;
            for (i = 0; i < so->size; i++) {
                snprintf(no->fields[i].key, sizeof(no->fields[i].key), "%s",
                         so->fields[i].key);
                no->fields[i].val = copy_val_rec(&so->fields[i].val, src, dst,
                                                 arena, seen, nseen, scap);
            }
        }
        return out;
    }
    return make_undef();
}

/* Copies a far-call return into the caller before the inner arena and
 * program are freed. Ints, floats, and undefined are returned as-is.
 * Strings are interned into dst. Arrays and field objects, including
 * nested values, are allocated in the caller arena. */
static GscVal copy_return(GscVal v, const GscProg* src, GscProg* dst, GscArena* arena) {
    GscSeen* seen = NULL;
    int nseen = 0;
    int scap = 0;
    GscVal out;
    if (v.kind != V_STR && v.kind != V_ISTR && v.kind != V_ARRAY && v.kind != V_OBJECT) return v;
    out = copy_val_rec(&v, src, dst, arena, &seen, &nseen, &scap);
    free(seen);
    return out;
}

/* Runs the program starting at entry_func. Returns 1 on normal exit, 0 if
 * the entry function could not be entered, the run ran off the end, or
 * fuel was exhausted. *ret_out receives the return value of the entry
 * function. entry_args are bound to the entry parameters (missing ones stay
 * undefined, extras are ignored). depth is the number of frames already
 * active outside this program; frames here count toward the same cap.
 * When ret_arena is set, the entry return is copied into that arena (and
 * ret_prog's string table) before this run's arena is freed. */
static int exec_prog(GscProg* prog, const GscFunc* entry_func,
                     GscHost* host, GscVal* ret_out,
                     const GscVal* entry_args, int entry_argc, int depth,
                     GscProg* ret_prog, GscArena* ret_arena);

/* Loads path through host->read_file, compiles it, and runs fname.
 * A missing file or function sets *out to undefined and returns 1.
 * Returns 0 only when the inner run stops the whole execution. */
static int call_far(GscProg* caller, GscHost* host, const char* path,
                    const char* fname, const GscVal* args, int argc,
                    int depth, GscVal* out, GscArena* caller_arena) {
    char* text = NULL;
    size_t text_sz = 0;
    GscProg child;
    const GscFunc* entry = NULL;
    GscVal* pass = NULL;
    int i, k, ok;

    *out = make_undef();
    if (!host || !host->read_file || !path || !fname) return 1;
    if (host->read_file(host->user, path, &text, &text_sz) != 1 || !text)
        return 1;
    if (!gsc_compile(text, text_sz, &child)) {
        free(text);
        return 1;
    }
    for (i = 0; i < child.nfunc; i++) {
        if (strcmp(child.funcs[i].name, fname) == 0) {
            entry = &child.funcs[i];
            break;
        }
    }
    if (!entry || entry->entry < 0) {
        gsc_prog_free(&child);
        free(text);
        return 1;
    }
    if (argc > 0) {
        pass = (GscVal*)malloc((size_t)argc * sizeof(GscVal));
        if (!pass) {
            gsc_prog_free(&child);
            free(text);
            return 1;
        }
        for (k = 0; k < argc; k++) pass[k] = args[k];
    }
    ok = exec_prog(&child, entry, host, out, pass, argc, depth,
                   caller, caller_arena);
    free(pass);
    gsc_prog_free(&child);
    free(text);
    return ok;
}

static int exec_prog(GscProg* prog, const GscFunc* entry_func,
                     GscHost* host, GscVal* ret_out,
                     const GscVal* entry_args, int entry_argc, int depth,
                     GscProg* ret_prog, GscArena* ret_arena) {
    GscVal stack[GSC_STACK_MAX];
    GscFrame frames[GSC_FRAME_MAX];
    GscArena arena;
    int sp = 0;
    int fp = 0;
    int ip;
    int fuel;
    char allies[64];
    char axis[64];

    allies[0] = '\0';
    axis[0] = '\0';
    arena.items = NULL;
    arena.kinds = NULL;
    arena.n = 0;
    arena.cap = 0;
    fuel = prog->fuel;

    if (entry_func->entry < 0) return 0;

    frames[0].return_ip = -1;
    frames[0].nlocals = entry_func->nlocals;
    frames[0].locals = NULL;
    frames[0].from_thread = 0;
    frames[0].sp_base = 0;
    if (entry_func->nlocals > 0) {
        frames[0].locals = (GscVal*)calloc((size_t)entry_func->nlocals,
                                           sizeof(GscVal));
        if (!frames[0].locals) return 0;
    }
    fp = 1;
    ip = entry_func->entry;
    if (entry_args && frames[0].locals && entry_argc > 0 && entry_func->nparams > 0) {
        int take = entry_argc < entry_func->nparams ? entry_argc : entry_func->nparams;
        int k;
        for (k = 0; k < take; k++) {
            /* ret_prog owns the caller's string table. Copy strings, arrays,
             * and field objects into this program before the callee runs. */
            if (ret_prog)
                frames[0].locals[k] = copy_return(entry_args[k], ret_prog, prog, &arena);
            else
                frames[0].locals[k] = entry_args[k];
        }
    }

    for (;;) {
        const GscIns* ins;
        if (fuel <= 0) {
            /* Fuel exhausted. Stop the whole run and tell the caller. */
            while (fp > 0) {
                free(frames[fp - 1].locals);
                fp--;
            }
            arena_free(&arena);
            if (ret_out) *ret_out = make_undef();
            return 0;
        }
        fuel--;
        if (ip < 0 || ip >= prog->ncode) break;
        ins = &prog->code[ip];
        switch (ins->op) {
        case GSC_RETURN: {
            GscVal rv = (sp > 0) ? stack[--sp] : make_undef();
            GscFrame* f = &frames[fp - 1];
            int ret_ip = f->return_ip;
            free(f->locals);
            fp--;
            if (fp == 0) {
                if (ret_out) {
                    if (ret_arena && ret_prog)
                        *ret_out = copy_return(rv, prog, ret_prog, ret_arena);
                    else
                        *ret_out = rv;
                }
                arena_free(&arena);
                return 1;
            }
            if (sp < GSC_STACK_MAX) stack[sp++] = rv;
            ip = ret_ip;
            continue;
        }
        case GSC_PUSH_UNDEF:
            if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
            break;
        case GSC_PUSH_INT:
            if (sp < GSC_STACK_MAX) stack[sp++] = make_int(ins->a);
            break;
        case GSC_BITAND: case GSC_BITOR: case GSC_BITXOR: {
            GscVal right = sp ? stack[--sp] : make_undef();
            GscVal left = sp ? stack[--sp] : make_undef();
            GscVal out = make_undef();
            if (left.kind == V_INT && right.kind == V_INT)
                out = make_int(ins->op == GSC_BITAND ? left.i & right.i :
                               ins->op == GSC_BITOR ? left.i | right.i : left.i ^ right.i);
            stack[sp++] = out;
            break;
        }
        case GSC_NEG:
            if (sp > 0) stack[sp - 1] = val_neg(&stack[sp - 1]);
            break;
        case GSC_VECTOR: {
            GscVal out = make_undef();
            int i;
            if (sp < 3) break;
            out.kind = V_VECTOR;
            for (i = 2; i >= 0; i--) {
                GscVal value = stack[--sp];
                if (value.kind == V_INT) out.vector[i] = value.i;
                else if (value.kind == V_FLOAT) out.vector[i] = value.f;
                else out.kind = V_UNDEF;
            }
            stack[sp++] = out;
            break;
        }
        case GSC_PUSH_FLOAT:
            if (sp < GSC_STACK_MAX) stack[sp++] = make_float(ins->f);
            break;
        case GSC_PUSH_STR: case GSC_PUSH_ISTR: {
            GscVal v = make_undef();
            v.kind = ins->op == GSC_PUSH_ISTR ? V_ISTR : V_STR;
            v.s = ins->str;
            if (sp < GSC_STACK_MAX) stack[sp++] = v;
            break;
        }
        case GSC_GET_LOCAL: {
            GscFrame* f = &frames[fp - 1];
            if (ins->a >= 0 && ins->a < f->nlocals) {
                if (ins->b && f->locals[ins->a].kind == V_UNDEF)
                    f->locals[ins->a] = arena_array_value(&arena);
                if (sp < GSC_STACK_MAX) stack[sp++] = f->locals[ins->a];
            } else if (sp < GSC_STACK_MAX) {
                stack[sp++] = make_undef();
            }
            break;
        }
        case GSC_SET_LOCAL: {
            GscFrame* f = &frames[fp - 1];
            GscVal v = (sp > 0) ? stack[--sp] : make_undef();
            if (ins->a >= 0 && ins->a < f->nlocals) f->locals[ins->a] = v;
            /* else: discard (scratch slot that was never allocated). */
            break;
        }
        case GSC_ADD: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = val_add(&a, &b, prog);
            break;
        }
        case GSC_SUB: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = val_sub(&a, &b);
            break;
        }
        case GSC_MUL: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = val_mul(&a, &b);
            break;
        }
        case GSC_DIV: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = val_div(&a, &b);
            break;
        }
        case GSC_LT: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = make_int(val_lt(&a, &b) ? 1 : 0);
            break;
        }
        case GSC_GT: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = make_int(val_gt(&a, &b) ? 1 : 0);
            break;
        }
        case GSC_EQ: {
            GscVal b = (sp > 0) ? stack[--sp] : make_undef();
            GscVal a = (sp > 0) ? stack[--sp] : make_undef();
            if (sp < GSC_STACK_MAX) stack[sp++] = make_int(val_eq(&a, &b) ? 1 : 0);
            break;
        }
        case GSC_JUMP:
            ip = ins->a;
            continue;
        case GSC_JUMP_BACK:
            /* Backward edge of a loop. Counted like any other instruction
             * for fuel; distinct opcode so the compiler and reader can
             * tell loop back edges apart from forward jumps. */
            ip = ins->a;
            continue;
        case GSC_JUMP_IF_FALSE: {
            GscVal v = (sp > 0) ? stack[--sp] : make_undef();
            if (!val_truthy(&v)) {
                ip = ins->a;
                continue;
            }
            break;
        }
        case GSC_NEW_ARRAY: {
            GscArr* a = arena_new_arr(&arena);
            GscVal v = make_undef();
            v.kind = V_ARRAY;
            v.p = a;
            if (sp < GSC_STACK_MAX) stack[sp++] = v;
            break;
        }
        case GSC_GET_INDEX: {
            GscVal idx = (sp > 0) ? stack[--sp] : make_undef();
            GscVal base = (sp > 0) ? stack[--sp] : make_undef();
            GscVal out = make_undef();
            if (base.kind == V_VECTOR && idx.kind == V_INT && idx.i >= 0 && idx.i < 3) {
                out = make_float(base.vector[idx.i]);
            } else if (base.kind == V_ARRAY && base.p && idx.kind == V_INT) {
                GscArr* a = (GscArr*)base.p;
                if (idx.i >= 0 && idx.i < a->size) out = a->items[idx.i];
            } else if (base.kind == V_ARRAY && base.p && idx.kind == V_STR) {
                out = arr_get_string((GscArr*)base.p, idx.s);
            } else if (base.kind == V_OBJECT && base.p && idx.kind == V_STR) {
                int fi = obj_find((GscObj*)base.p, str_at(prog, idx.s));
                if (fi >= 0) out = ((GscObj*)base.p)->fields[fi].val;
            }
            if (sp < GSC_STACK_MAX) stack[sp++] = out;
            break;
        }
        case GSC_SET_INDEX: {
            GscVal val = (sp > 0) ? stack[--sp] : make_undef();
            GscVal idx = (sp > 0) ? stack[--sp] : make_undef();
            GscVal base = (sp > 0) ? stack[--sp] : make_undef();
            if (base.kind == V_ARRAY && base.p && idx.kind == V_INT) {
                arr_set((GscArr*)base.p, idx.i, val);
            } else if (base.kind == V_ARRAY && base.p && idx.kind == V_STR) {
                arr_set_string((GscArr*)base.p, idx.s, val);
            } else if (base.kind == V_OBJECT && base.p && idx.kind == V_STR) {
                obj_set((GscObj*)base.p, str_at(prog, idx.s), val);
            }
            break;
        }
        case GSC_GET_FIELD: {
            GscVal base = (sp > 0) ? stack[--sp] : make_undef();
            GscVal out = make_undef();
            const char* key = str_at(prog, ins->a);
            if (base.kind == V_ARRAY && key && strcmp(key, "size") == 0) {
                GscArr* a = (GscArr*)base.p;
                out = make_int(a ? a->count + a->nstrings : 0);
            } else if (base.kind == V_OBJECT && base.p) {
                int fi = obj_find((GscObj*)base.p, key);
                if (fi >= 0) out = ((GscObj*)base.p)->fields[fi].val;
            }
            if (sp < GSC_STACK_MAX) stack[sp++] = out;
            break;
        }
        case GSC_SET_FIELD: {
            GscVal val = (sp > 0) ? stack[--sp] : make_undef();
            GscVal base = (sp > 0) ? stack[--sp] : make_undef();
            const char* key = str_at(prog, ins->a);
            if (base.kind == V_OBJECT && base.p) {
                obj_set((GscObj*)base.p, key, val);
            }
            break;
        }
        case GSC_CALL: {
            int fi = ins->a;
            int argc = ins->b;
            const GscFunc* ff;
            GscVal* nl = NULL;
            int take;
            int k;

            if (fi < 0 || fi >= prog->nfunc) {
                sp -= argc;
                if (sp < 0) sp = 0;
                if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                break;
            }
            ff = &prog->funcs[fi];
            if (depth + fp >= GSC_CALL_DEPTH_CAP || fp >= GSC_FRAME_MAX ||
                ff->entry < 0) {
                /* Depth cap or missing body: push undefined, keep caller. */
                sp -= argc;
                if (sp < 0) sp = 0;
                if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                break;
            }
            if (ff->nlocals > 0) {
                nl = (GscVal*)calloc((size_t)ff->nlocals, sizeof(GscVal));
                if (!nl) {
                    sp -= argc;
                    if (sp < 0) sp = 0;
                    if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                    break;
                }
            }
            take = argc < ff->nparams ? argc : ff->nparams;
            for (k = 0; k < take; k++) nl[k] = stack[sp - argc + k];
            sp -= argc;
            if (sp < 0) sp = 0;
            frames[fp].return_ip = ip + 1;
            frames[fp].nlocals = ff->nlocals;
            frames[fp].locals = nl;
            frames[fp].from_thread = 0;
            frames[fp].sp_base = sp;
            fp++;
            ip = ff->entry;
            continue;
        }
        case GSC_FAR: {
            int argc_n = ins->str;
            GscVal retv;
            int ok;
            if (argc_n < 0) argc_n = 0;
            if (argc_n > sp) argc_n = sp;
            if (depth + fp >= GSC_CALL_DEPTH_CAP || fp >= GSC_FRAME_MAX) {
                sp -= argc_n;
                if (sp < 0) sp = 0;
                if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                break;
            }
            ok = call_far(prog, host, str_at(prog, ins->a), str_at(prog, ins->b),
                          (argc_n > 0) ? &stack[sp - argc_n] : NULL,
                          argc_n, depth + fp, &retv, &arena);
            sp -= argc_n;
            if (sp < 0) sp = 0;
            if (!ok) {
                while (fp > 0) {
                    free(frames[fp - 1].locals);
                    fp--;
                }
                arena_free(&arena);
                if (ret_out) *ret_out = make_undef();
                return 0;
            }
            if (sp < GSC_STACK_MAX) stack[sp++] = retv;
            break;
        }
        case GSC_THREAD: {
            /* Far thread: str >= 0, same operands as GSC_FAR. The child
             * program is the whole branch, so a GSC_STOP there returns here
             * and the caller keeps going. One run, no second schedule. */
            if (ins->str >= 0) {
                int argc_n = ins->str;
                GscVal retv;
                int ok;
                if (argc_n > sp) argc_n = sp;
                if (depth + fp >= GSC_CALL_DEPTH_CAP || fp >= GSC_FRAME_MAX) {
                    sp -= argc_n;
                    if (sp < 0) sp = 0;
                    if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                    break;
                }
                ok = call_far(prog, host, str_at(prog, ins->a), str_at(prog, ins->b),
                              (argc_n > 0) ? &stack[sp - argc_n] : NULL,
                              argc_n, depth + fp, &retv, &arena);
                sp -= argc_n;
                if (sp < 0) sp = 0;
                if (!ok) {
                    while (fp > 0) {
                        free(frames[fp - 1].locals);
                        fp--;
                    }
                    arena_free(&arena);
                    if (ret_out) *ret_out = make_undef();
                    return 0;
                }
                if (sp < GSC_STACK_MAX) stack[sp++] = retv;
                break;
            }
            /* Local thread: same as GSC_CALL, but this frame is a branch
             * GSC_STOP can cut back to the instruction after the thread. */
            {
                int fi = ins->a;
                int argc = ins->b;
                const GscFunc* ff;
                GscVal* nl = NULL;
                int take;
                int k;

                if (fi < 0 || fi >= prog->nfunc) {
                    sp -= argc;
                    if (sp < 0) sp = 0;
                    if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                    break;
                }
                ff = &prog->funcs[fi];
                if (depth + fp >= GSC_CALL_DEPTH_CAP || fp >= GSC_FRAME_MAX ||
                    ff->entry < 0) {
                    sp -= argc;
                    if (sp < 0) sp = 0;
                    if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                    break;
                }
                if (ff->nlocals > 0) {
                    nl = (GscVal*)calloc((size_t)ff->nlocals, sizeof(GscVal));
                    if (!nl) {
                        sp -= argc;
                        if (sp < 0) sp = 0;
                        if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                        break;
                    }
                }
                take = argc < ff->nparams ? argc : ff->nparams;
                for (k = 0; k < take; k++) nl[k] = stack[sp - argc + k];
                sp -= argc;
                if (sp < 0) sp = 0;
                frames[fp].return_ip = ip + 1;
                frames[fp].nlocals = ff->nlocals;
                frames[fp].locals = nl;
                frames[fp].from_thread = 1;
                frames[fp].sp_base = sp;
                fp++;
                ip = ff->entry;
                continue;
            }
        }
        case GSC_STOP: {
            /* End this branch. Ordinary calls inside it unwind too, until
             * the frame the thread entered. That caller continues. A stop
             * in the entry function ends the run with success; host calls
             * already made stay. */
            for (;;) {
                GscFrame* f;
                int ret_ip;
                int from_thread;
                int sp_base;
                if (fp <= 0) {
                    arena_free(&arena);
                    if (ret_out) *ret_out = make_undef();
                    return 1;
                }
                f = &frames[fp - 1];
                ret_ip = f->return_ip;
                from_thread = f->from_thread;
                sp_base = f->sp_base;
                free(f->locals);
                f->locals = NULL;
                fp--;
                if (fp == 0) {
                    if (ret_out) *ret_out = make_undef();
                    arena_free(&arena);
                    return 1;
                }
                if (from_thread) {
                    if (sp_base < 0) sp_base = 0;
                    if (sp_base > sp) sp_base = sp;
                    sp = sp_base;
                    if (sp < GSC_STACK_MAX) stack[sp++] = make_undef();
                    ip = ret_ip;
                    break;
                }
            }
            continue;
        }
        case GSC_BUILTIN: {
            int id = ins->a;
            int argc = ins->b;
            GscVal* args = (argc > 0) ? &stack[sp - argc] : NULL;
            if (id == GSC_BU_ALLIES && argc > 0 && args[0].kind == V_STR) {
                const char* s = str_at(prog, args[0].s);
                snprintf(allies, sizeof(allies), "%s", s ? s : "");
                if (allies[0] && axis[0] && host && host->set_team_names)
                    host->set_team_names(host->user, allies, axis);
            } else if (id == GSC_BU_AXIS && argc > 0 && args[0].kind == V_STR) {
                const char* s = str_at(prog, args[0].s);
                snprintf(axis, sizeof(axis), "%s", s ? s : "");
                if (allies[0] && axis[0] && host && host->set_team_names)
                    host->set_team_names(host->user, allies, axis);
            } else if (id == GSC_BU_STRUCT) {
                /* spawnstruct(): discard any stray args, push an empty
                 * field object. */
                GscObj* o;
                GscVal v;
                sp -= argc;
                if (sp < 0) sp = 0;
                o = arena_new_obj(&arena);
                v = make_undef();
                v.kind = V_OBJECT;
                v.p = o;
                if (sp < GSC_STACK_MAX) stack[sp++] = v;
                break;
            } else if (id != 0) {
                builtin_call(host, id, args, argc, prog);
            }
            {
                GscVal handle = (id == GSC_BU_LOAD_FX && argc > 0 && args[0].kind == V_STR) ? args[0] : make_undef();
                sp -= argc;
                if (sp < 0) sp = 0;
                if (id == GSC_BU_LOAD_FX && sp < GSC_STACK_MAX) stack[sp++] = handle;
            }
            break;
        }
        default:
            /* Unknown opcode: stop the run cleanly. */
            while (fp > 0) {
                free(frames[fp - 1].locals);
                fp--;
            }
            arena_free(&arena);
            if (ret_out) *ret_out = make_undef();
            return 0;
        }
        ip++;
    }

    /* Ran off the end without hitting a terminal return. Clean up. */
    while (fp > 0) {
        free(frames[fp - 1].locals);
        fp--;
    }
    arena_free(&arena);
    if (ret_out) *ret_out = make_undef();
    return 0;
}

int gsc_vm_exec(const char* source, size_t size, const char* func, GscHost* host) {
    GscProg prog;
    const GscFunc* entry = NULL;
    GscVal ret;
    int ok;
    int i;

    if (!source || !func) return 0;
    gsc_vm_last_int = 0;
    if (!gsc_compile(source, size, &prog)) return 0;
    for (i = 0; i < prog.nfunc; i++) {
        if (strcmp(prog.funcs[i].name, func) == 0) {
            entry = &prog.funcs[i];
            break;
        }
    }
    if (!entry) {
        gsc_prog_free(&prog);
        return 0;
    }
    ret = make_undef();
    ok = exec_prog(&prog, entry, host, &ret, NULL, 0, 0, NULL, NULL);
    if (ok && ret.kind == V_INT) gsc_vm_last_int = ret.i;
    gsc_prog_free(&prog);
    return ok;
}

int gsc_vm_scan_weapons(const char* source, size_t size, char names[][64], int max_names) {
    const char* p;
    const char* end;
    int n = 0;
    if (!source || !names || max_names <= 0) return 0;
    p = source;
    end = source + size;
    while (p < end && n < max_names) {
        const char* hit = strstr(p, "giveWeapon");
        const char* q;
        size_t k = 0;
        if (!hit || hit >= end) break;
        q = strchr(hit, '"');
        if (!q || q >= end) break;
        q++;
        while (q < end && *q != '"' && k + 1 < 64) names[n][k++] = *q++;
        names[n][k] = '\0';
        if (k > 0) n++;
        p = q + 1;
    }
    return n;
}
/* Persistent map-owned coroutine runtime. No file reads occur in tick(). */
#define RT_THREADS 256
#define RT_FILES 1024
#define RT_EVENTS 16
typedef struct RtFile { char path[256]; int first, count, code_first, code_count, valid; int* imports; int nimports; } RtFile;
typedef struct RtEvent { GscVal receiver; int name; } RtEvent;
typedef struct RtThread {
    GscVal stack[GSC_STACK_MAX], self, payload[GSC_ARGUMENT_MAX];
    GscVal match[GSC_ARGUMENT_MAX]; int match_count;
    GscFrame frames[GSC_FRAME_MAX];
    int main_root,start_root;
    int sp, fp, ip, state, event_name, payload_count;
    GscVal event_receiver;
    int64_t wake;
    unsigned epoch, frame_end_epoch;
    RtEvent endon[RT_EVENTS]; int nendon;
} RtThread;
struct GscRuntime {
    GscHost host; GscProg prog; GscArena arena;
    GscVal level, game, anim;
    RtFile files[RT_FILES]; int nfiles;
    RtThread* threads[RT_THREADS];
    int errors, now, budget, running, loading, start_requested, start_dispatched, start_function, start_complete;
    int native_field_failure;
    unsigned epoch, cursor;
    uint32_t random_state;
    int linked_code;
    int closed_link, sealed, link_failed;
    GscNativeBinding* bindings; int nbindings;
    int retire_serial;
    unsigned char marks[8192];
    int mark_stack[8192], mark_count;
};
static void rt_error(GscRuntime* r, const char* message) {
    if(r->errors<INT_MAX)r->errors++;
    if(r->errors<=16 && r->host.error)r->host.error(r->host.user,message);
}
static GscVal rt_string(GscRuntime* r, const char* text) {
    GscVal v=make_undef(); int n=prog_intern(&r->prog,text?text:"");
    if(n>=0) { v.kind=V_STR; v.s=n; } else rt_error(r,"GSC string allocation failed");
    return v;
}
static GscVal rt_entity(GscRuntime* r,int id) {
    GscVal v=make_undef(); int i; GscObj* o;
    for(i=0;i<r->arena.n;i++) if(r->arena.kinds[i]==1) {
        o=(GscObj*)r->arena.items[i];
        if(o->is_entity && !o->deleted && o->entity_id==id) { v.kind=V_OBJECT;v.p=o;return v; }
    }
    o=arena_new_obj(&r->arena);
    if(o) {o->is_entity=1;o->entity_id=id;v.kind=V_OBJECT;v.p=o;}
    return v;
}
static int rt_id(GscVal v,int* id) {
    if(v.kind!=V_OBJECT || !v.p || !((GscObj*)v.p)->is_entity || ((GscObj*)v.p)->deleted) return 0;
    *id=((GscObj*)v.p)->entity_id; return 1;
}
static void rt_free_thread(RtThread* t) {
    int i;if(!t)return;for(i=0;i<t->fp;i++)free(t->frames[i].locals);free(t);
}
static void rt_mark(GscRuntime* r,GscVal v) {
    int index;
    if(!v.p || (v.kind!=V_ARRAY && v.kind!=V_OBJECT))return;
    index=v.kind==V_ARRAY?((GscArr*)v.p)->arena_index:((GscObj*)v.p)->arena_index;
    if(index<0 || index>=r->arena.n || r->arena.items[index]!=v.p || r->marks[index])return;
    r->marks[index]=1;r->mark_stack[r->mark_count++]=index;
}
static void rt_collect(GscRuntime* r) {
    int i,j,k,n=0;
    if(r->arena.n<4096)return;
    memset(r->marks,0,(size_t)r->arena.n);r->mark_count=0;
    rt_mark(r,r->level);rt_mark(r,r->game);rt_mark(r,r->anim);
    /* Entity script fields persist even when no thread holds the wrapper. */
    for(i=0;i<r->arena.n;i++)if(r->arena.kinds[i]==1 && ((GscObj*)r->arena.items[i])->is_entity && !((GscObj*)r->arena.items[i])->deleted && !((GscObj*)r->arena.items[i])->retired) {
        GscVal v=make_undef();v.kind=V_OBJECT;v.p=r->arena.items[i];rt_mark(r,v);
    }
    for(i=0;i<RT_THREADS;i++) {
        RtThread* t=r->threads[i];if(!t || t->state==3)continue;
        rt_mark(r,t->self);
        for(j=0;j<t->sp;j++)rt_mark(r,t->stack[j]);
        for(j=0;j<t->payload_count;j++)rt_mark(r,t->payload[j]);
        for(j=0;j<t->match_count;j++)rt_mark(r,t->match[j]);
        if(t->state==2)rt_mark(r,t->event_receiver);
        for(j=0;j<t->nendon;j++)rt_mark(r,t->endon[j].receiver);
        for(j=0;j<t->fp;j++) {
            rt_mark(r,t->frames[j].saved_self);
            for(k=0;k<t->frames[j].nlocals;k++)rt_mark(r,t->frames[j].locals[k]);
        }
    }
    while(r->mark_count) {
        i=r->mark_stack[--r->mark_count];
        if(r->arena.kinds[i]==0) {
            GscArr* a=(GscArr*)r->arena.items[i];for(j=0;j<a->size;j++)rt_mark(r,a->items[j]);
            for(j=0;j<a->nstrings;j++)rt_mark(r,a->strings[j].value);
        } else {
            GscObj* o=(GscObj*)r->arena.items[i];for(j=0;j<o->size;j++)rt_mark(r,o->fields[j].val);
        }
    }
    for(i=0;i<r->arena.n;i++) {
        void* item=r->arena.items[i];int kind=r->arena.kinds[i];
        if(!r->marks[i]) {
            if(kind==0){free(((GscArr*)item)->items);free(((GscArr*)item)->strings);free(item);}
            else {free(((GscObj*)item)->fields);free(item);}
        } else {
            r->arena.items[n]=item;r->arena.kinds[n]=kind;
            if(kind==0)((GscArr*)item)->arena_index=n;else ((GscObj*)item)->arena_index=n;
            n++;
        }
    }
    r->arena.n=n;
}
static int rt_load(GscRuntime *r, const char *path, const char *source, size_t size) {
    GscProg p;
    int i, j, index, base, fb;
    int *strings;
    char *owned = NULL;
    char normalized[256];
    size_t length;
    if (!path || (length = strlen(path)) >= sizeof(normalized)) {
        rt_error(r, "GSC invalid script path");
        return -1;
    }
    for (i = 0; i < (int)length; i++)
        normalized[i] = path[i] == '\\' ? '/' : (char)tolower((unsigned char)path[i]);
    normalized[length] = 0;
    path = normalized;
    for (i = 0; i < r->nfiles; i++)
        if (strcmp(r->files[i].path, path) == 0)
            return r->files[i].valid ? i : -1;
    if(r->closed_link && r->sealed){rt_error(r,"GSC linked program is sealed; source loading forbidden");return -1;}
    if (r->nfiles >= RT_FILES || strlen(path) >= 256) {
        rt_error(r, "GSC file limit");
        return -1;
    }
    if (!source) {
        if (!r->host.read_file || !r->host.read_file(r->host.user, path, &owned, &size))
            return -1;
        source = owned;
    }
    {
        GscCompileDiagnostic diagnostic;
        GscCompileOptions options = {0};
        if (r->host.get_cvar) {
            const char *developer = r->host.get_cvar(r->host.user, "developer");
            options.developer = developer && atoi(developer) != 0;
        }
        if (!gsc_compile_runtime_options(source, size, &p, &options, &diagnostic)) {
            char message[512];
            snprintf(message, sizeof(message), "%s:%u:%u: %s", path, diagnostic.line,
                     diagnostic.column, diagnostic.message);
            free(owned);
            rt_error(r, message);
            return -1;
        }
    }
    free(owned);
    base = r->prog.ncode;
    fb = r->prog.nfunc;
    if (p.ncode > 1000000 - base || p.nfunc > 8192 - fb) {
        gsc_prog_free(&p);
        rt_error(r, "GSC program limit");
        return -1;
    }
    strings = (int *)malloc((size_t)(p.nstr ? p.nstr : 1) * sizeof(int));
    if (!strings) {
        gsc_prog_free(&p);
        return -1;
    }
    for (i = 0; i < p.nstr; i++) {
        strings[i] = prog_intern(&r->prog, str_at(&p, i));
        if (strings[i] < 0)
            goto fail;
    }
    {
        GscIns *code = base + p.ncode ? (GscIns *)realloc(r->prog.code,
                                                          (size_t)(base + p.ncode) * sizeof(GscIns))
                                      : r->prog.code;
        GscFunc *funcs;
        if (base + p.ncode && !code)
            goto fail;
        r->prog.code = code;
        funcs = fb + p.nfunc
                    ? (GscFunc *)realloc(r->prog.funcs, (size_t)(fb + p.nfunc) * sizeof(GscFunc))
                    : r->prog.funcs;
        if (fb + p.nfunc && !funcs)
            goto fail;
        r->prog.funcs = funcs;
    }
    for (i = 0; i < p.ncode; i++) {
        GscIns in = p.code[i];
        if (in.op == GSC_PUSH_STR || in.op == GSC_PUSH_ISTR || in.op == GSC_DIAGNOSTIC ||
            (in.op == GSC_BUILTIN && in.a == 0 && in.str >= 0))
            in.str = strings[in.str];
        if (in.op == GSC_GET_FIELD || in.op == GSC_SET_FIELD || in.op == GSC_METHOD)
            in.a = strings[in.a];
        if (in.op == GSC_IMPORT || in.op == GSC_NAMED_CALL || in.op == GSC_NAMED_REF ||
            in.op == GSC_ANIMTREE)
            in.a = strings[in.a];
        if (in.op == GSC_ANIMATION) {
            in.a = strings[in.a];
            in.b = strings[in.b];
        }
        if (in.op == GSC_FAR_REF || in.op == GSC_FAR || in.op == GSC_ENTITY_FAR ||
            ((in.op == GSC_THREAD || in.op == GSC_ENTITY_THREAD) && in.str >= 0)) {
            in.a = strings[in.a];
            in.b = strings[in.b];
        }
        if (in.op == GSC_FUNC_REF || in.op == GSC_CALL || in.op == GSC_ENTITY_CALL ||
            ((in.op == GSC_THREAD || in.op == GSC_ENTITY_THREAD) && in.str < 0))
            in.a += fb;
        if (in.op == GSC_JUMP || in.op == GSC_JUMP_BACK || in.op == GSC_JUMP_IF_FALSE)
            in.a += base;
        r->prog.code[base + i] = in;
    }
    for (i = 0; i < p.nfunc; i++) {
        r->prog.funcs[fb + i] = p.funcs[i];
        r->prog.funcs[fb + i].entry += base;
    }
    r->prog.ncode = base + p.ncode;
    r->prog.nfunc = fb + p.nfunc;
    index = r->nfiles++;
    snprintf(r->files[index].path, 256, "%s", path);
    r->files[index].first = fb;
    r->files[index].count = p.nfunc;
    r->files[index].code_first = base;
    r->files[index].code_count = p.ncode;
    r->files[index].valid = 1;
    free(strings);
    gsc_prog_free(&p);
    /* Literal sound dependencies in compiled code, including uncalled functions.
     * Comments/strings cannot accidentally match method names. No gameplay I/O. */
    if(r->host.precache)for(i=base+1;i<r->prog.ncode;i++) {
        GscIns call=r->prog.code[i],arg=r->prog.code[i-1];
        if(call.op==GSC_METHOD && call.b==1 && arg.op==GSC_PUSH_STR) {
            const char* method=str_at(&r->prog,call.a);
            if(name_equal(method,"playSound") || name_equal(method,"playLoopSound") || name_equal(method,"playLocalSound"))r->host.precache(r->host.user,GSC_PC_SOUND,str_at(&r->prog,arg.str));
        }
    }
    if(r->host.animation_reference)for(i=base;i<r->prog.ncode;++i){
        const GscIns reference=r->prog.code[i];
        if(reference.op==GSC_ANIMATION || reference.op==GSC_ANIMTREE)
            r->host.animation_reference(r->host.user,str_at(&r->prog,reference.a),reference.op==GSC_ANIMATION?str_at(&r->prog,reference.b):NULL);
    }
    /* Resolve imports during loading; missing optional scripts stay unresolved. */
    j = r->prog.ncode;
    for (i = base; i < j; i++) {
        GscIns in = r->prog.code[i];
        if (in.op == GSC_IMPORT || in.op == GSC_FAR_REF || in.op == GSC_FAR ||
            in.op == GSC_ENTITY_FAR ||
            ((in.op == GSC_THREAD || in.op == GSC_ENTITY_THREAD) && in.str >= 0)) {
            char dep[256];
            snprintf(dep, sizeof(dep), "%s", str_at(&r->prog, in.a));
            int dependency = rt_load(r, dep, NULL, 0);
            if (in.op == GSC_IMPORT) {
                int *imports;
                if (dependency < 0) {
                    char message[576];
                    snprintf(message, sizeof(message), "GSC missing include %s (from %s)", dep,
                             path);
                    rt_error(r, message);
                    r->files[index].valid = 0;
                    continue;
                }
                imports = (int *)realloc(r->files[index].imports,
                                         (size_t)(r->files[index].nimports + 1) * sizeof(int));
                if (!imports) {
                    rt_error(r, "GSC import allocation failed");
                    r->files[index].valid = 0;
                    continue;
                }
                r->files[index].imports = imports;
                imports[r->files[index].nimports++] = dependency;
            }
        }
    }
    return r->files[index].valid ? index : -1;
fail:
    free(strings);
    gsc_prog_free(&p);
    rt_error(r, "GSC program allocation failed");
    return -1;
}
static int rt_function(GscRuntime *r, int file, const char *name) {
    int i;
    if (file < 0 || !r->files[file].valid)
        return -1;
    for (i = r->files[file].first; i < r->files[file].first + r->files[file].count; i++)
        if (name_equal(r->prog.funcs[i].name, name))
            return i;
    return -1;
}
static int rt_resolve(GscRuntime *r, int file, const char *name, unsigned char *visited) {
    int own, i, result = -1;
    if (file < 0 || file >= r->nfiles || !r->files[file].valid || visited[file])
        return -1;
    own = rt_function(r, file, name);
    if (own >= 0)
        return own;
    visited[file] = 1;
    for (i = 0; i < r->files[file].nimports; i++) {
        int fn = rt_resolve(r, r->files[file].imports[i], name, visited);
        if (fn == -2)
            return -2;
        if (fn >= 0) {
            if (result >= 0 && result != fn) {
                rt_error(r, "GSC ambiguous included function");
                return -2;
            }
            result = fn;
        }
    }
    return result;
}
static void rt_link(GscRuntime *r) {
    int file, i;
    if (r->linked_code == r->prog.ncode)
        return;
    for (file = 0; file < r->nfiles; file++)
        for (i = r->files[file].code_first;
             i < r->files[file].code_first + r->files[file].code_count; i++) {
            GscIns *in = &r->prog.code[i];
            int target = file, fn = -1, argc = in->b, flags = 0, ref = 0;
            const char *name = NULL;
            unsigned char visited[RT_FILES];
            if (in->op == GSC_NAMED_CALL || in->op == GSC_NAMED_REF || in->op == GSC_METHOD) {
                name = str_at(&r->prog, in->a);
                flags = in->op == GSC_METHOD ? 1 : in->str;
                ref = in->op == GSC_NAMED_REF;
            } else if (in->op == GSC_FAR_REF || in->op == GSC_FAR || in->op == GSC_ENTITY_FAR ||
                       ((in->op == GSC_THREAD || in->op == GSC_ENTITY_THREAD) && in->str >= 0)) {
                int k;
                const char *path = str_at(&r->prog, in->a);
                name = str_at(&r->prog, in->b);
                target = -1;
                for (k = 0; k < r->nfiles; k++)
                    if (!strcmp(r->files[k].path, path)) {
                        target = k;
                        break;
                    }
                ref = in->op == GSC_FAR_REF;
                argc = in->str;
                flags = (in->op == GSC_ENTITY_FAR || in->op == GSC_ENTITY_THREAD ? 1 : 0) |
                        (in->op == GSC_THREAD || in->op == GSC_ENTITY_THREAD ? 2 : 0);
            }
            if (!name)
                continue;
            memset(visited, 0, sizeof(visited));
            fn = rt_resolve(r, target, name, visited);
            if (fn < 0)
                continue;
            in->op = (unsigned char)(ref           ? GSC_FUNC_REF
                                     : (flags & 2) ? ((flags & 1) ? GSC_ENTITY_THREAD : GSC_THREAD)
                                                   : ((flags & 1) ? GSC_ENTITY_CALL : GSC_CALL));
            in->a = fn;
            in->b = ref ? 0 : argc;
            in->str = -1;
        }
    r->linked_code = r->prog.ncode;
}
static const char* const rt_intrinsics[]={"int","float","randomInt","randomFloat","getTime",
    "distance","length","vectorNormalize","vectorToAngles","anglesToForward","abs","sqrt","min","max"};
static const char* const rt_event_methods[]={"notify","waittill","waittillmatch","endon"};
static int rt_name_index(const char* name,const char* const* names,int count){
    int i;for(i=0;i<count;++i)if(name_equal(name,names[i]))return i;return -1;
}
int gsc_runtime_add_source(GscRuntime* r,const char* path,const char* source,size_t size){
    int file,errors;if(!r || !r->closed_link || r->sealed || r->link_failed || r->running)return 0;
    errors=r->errors;file=rt_load(r,path,source,size);
    if(file<0 || errors!=r->errors){r->link_failed=1;return 0;}return 1;
}
int gsc_runtime_require_entry(GscRuntime* r,const char* path,const char* function){
    int file;
    if(!r || !r->closed_link || r->sealed || r->link_failed || r->running || !path || !function)return 0;
    file=rt_load(r,path,NULL,0);
    if(rt_function(r,file,function)<0){
        char message[512];snprintf(message,sizeof(message),"%s: missing GSC entry: %s",path,function);
        rt_error(r,message);r->link_failed=1;return 0;
    }
    return 1;
}
int gsc_runtime_link(GscRuntime* r){
    int file,i,errors;if(!r || !r->closed_link || r->running || r->link_failed)return 0;
    if(r->sealed)return 1;
    errors=r->errors;rt_link(r);
    for(file=0;file<r->nfiles;++file){
        if(!r->files[file].valid){r->link_failed=1;continue;}
        for(i=r->files[file].code_first;i<r->files[file].code_first+r->files[file].code_count;++i){
            GscIns* in=&r->prog.code[i];int method=in->op==GSC_METHOD,slot=-1,k;
            const char* name=NULL;
            if(in->op==GSC_NAMED_CALL || method){
                name=str_at(&r->prog,in->a);
                if(method)slot=rt_name_index(name,rt_event_methods,4);
                else if(in->str==0)slot=rt_name_index(name,rt_intrinsics,(int)(sizeof(rt_intrinsics)/sizeof(*rt_intrinsics)));
                if(slot>=0){in->op=method?GSC_INTRINSIC_METHOD:GSC_INTRINSIC_CALL;in->a=slot;continue;}
                for(k=0;k<r->nbindings;++k)if(name_equal(name,r->bindings[k].name) &&
                    (r->bindings[k].flags&(method?GSC_BIND_METHOD:GSC_BIND_FUNCTION))){slot=k;break;}
                if(slot>=0 && (method || in->str==0)){in->op=GSC_NATIVE_SLOT;in->a=slot;in->str=method;continue;}
            }else if(in->op==GSC_NAMED_REF)name=str_at(&r->prog,in->a);
            else if(in->op==GSC_FAR || in->op==GSC_FAR_REF || in->op==GSC_ENTITY_FAR ||
                ((in->op==GSC_THREAD || in->op==GSC_ENTITY_THREAD) && in->str>=0))name=str_at(&r->prog,in->b);
            if(name){char message[512];snprintf(message,sizeof(message),"%s:%u:%u: unresolved GSC %s: %s (instruction %d)",
                r->files[file].path,in->line,in->column,method?"method":"call/reference",name,i-r->files[file].code_first);rt_error(r,message);r->link_failed=1;}
        }
    }
    if(errors!=r->errors)r->link_failed=1;
    if(r->link_failed)return 0;
    r->sealed=1;
    return 1;
}
static int rt_enter(GscRuntime* r,RtThread* t,int function,GscVal* args,int argc,GscVal self) {
    GscFrame* f;GscFunc* fn;int i;
    if(function<0 || function>=r->prog.nfunc || t->fp>=GSC_FRAME_MAX) {
        rt_error(r,"GSC invalid function or call depth limit");t->state=3;return 0;
    }
    fn=&r->prog.funcs[function];f=&t->frames[t->fp];memset(f,0,sizeof(*f));
    f->nlocals=fn->nlocals;f->locals=(GscVal*)calloc((size_t)(f->nlocals?f->nlocals:1),sizeof(GscVal));
    if(!f->locals){t->state=3;rt_error(r,"GSC frame allocation failed");return 0;}
    for(i=0;i<argc && i<fn->nparams && i<f->nlocals;i++)f->locals[i]=args[i];
    f->return_ip=t->fp?t->ip:-1;f->sp_base=t->sp;f->saved_self=t->self;
    t->fp++;t->self=self;t->ip=fn->entry;return 1;
}
static RtThread* rt_spawn(GscRuntime* r,int fn,GscVal* args,int argc,GscVal self) {
    int i;RtThread* t;
    if(r->closed_link && (!r->sealed || r->link_failed))return NULL;
    for(i=0;i<RT_THREADS;i++)if(!r->threads[i])break;
    if(i==RT_THREADS){rt_error(r,"GSC thread limit");return NULL;}
    t=(RtThread*)calloc(1,sizeof(*t));if(!t)return NULL;
    r->threads[i]=t;t->epoch=r->epoch;
    rt_enter(r,t,fn,args,argc,self);return t;
}
static int rt_notify_values(GscRuntime *r, GscVal receiver, int event, GscVal *args, int argc, RtThread* emitter) {
    int i, j, woken = 0;
    for (i = 0; i < RT_THREADS; i++) {
        RtThread *t = r->threads[i];
        if (!t || t->state == 3)
            continue;
        for (j = 0; j < t->nendon; j++)
            if (t->endon[j].receiver.p == receiver.p && t->endon[j].name == event) {
                if(t==emitter){
                    memmove(t->endon+j,t->endon+j+1,(size_t)(t->nendon-j-1)*sizeof(t->endon[0]));t->nendon--;j--;continue;
                }
                t->state = 3;
                break;
            }
        if (t->state == 2 && t->event_receiver.p == receiver.p && t->event_name == event) {
            if (argc < t->match_count)
                continue;
            for (j = 0; j < t->match_count; j++)
                if (!val_eq(&t->match[j], &args[j]))
                    break;
            if (j < t->match_count)
                continue;
            t->state = 0;
            t->epoch = r->epoch;
            t->payload_count = argc > GSC_ARGUMENT_MAX ? GSC_ARGUMENT_MAX : argc;
            t->match_count = 0;
            for (j = 0; j < t->payload_count; j++)
                t->payload[j] = args[j];
            woken++;
        }
    }
    return woken;
}
static GscVal rt_native_value(GscRuntime *r, const GscNativeValue *value, int depth) {
    GscVal out = make_undef();
    int i;
    if (!value || depth > 4)
        return out;
    switch (value->kind) {
    case GSC_NATIVE_INT:
        return make_int(value->integer);
    case GSC_NATIVE_FLOAT:
        return isfinite(value->number) ? make_float(value->number) : out;
    case GSC_NATIVE_STRING:
        return rt_string(r, value->text);
    case GSC_NATIVE_ENTITY:
        return rt_entity(r, value->integer);
    case GSC_NATIVE_ANIMATION:
    case GSC_NATIVE_ANIMTREE:
        if(!value->tree || !*value->tree ||
           (value->kind==GSC_NATIVE_ANIMATION && (!value->text || !*value->text)))return out;
        out.i=prog_intern(&r->prog,value->tree);
        out.s=value->kind==GSC_NATIVE_ANIMATION?prog_intern(&r->prog,value->text):out.i;
        if(out.i<0 || out.s<0){rt_error(r,"GSC animation allocation failed");return make_undef();}
        out.kind=value->kind==GSC_NATIVE_ANIMATION?V_ANIMATION:V_ANIMTREE;
        return out;
    case GSC_NATIVE_VECTOR:
        out.kind = V_VECTOR;
        for (i = 0; i < 3; i++) {
            if (!isfinite(value->vector[i]))
                return make_undef();
            out.vector[i] = value->vector[i];
        }
        return out;
    case GSC_NATIVE_RECORD:
        if (value->field_count < 0 || value->field_count > 32 ||
            (value->field_count && !value->fields))
            return out;
        out.p = arena_new_obj(&r->arena);
        if (!out.p) {
            rt_error(r, "GSC native record allocation failed");
            return out;
        }
        out.kind = V_OBJECT;
        for (i = 0; i < value->field_count; i++)
            if (value->fields[i].name)
                obj_set((GscObj *)out.p, value->fields[i].name,
                        rt_native_value(r, &value->fields[i].value, depth + 1));
        return out;
    default:
        return out;
    }
}
static int rt_native_field(GscRuntime *r, GscObj *o, const char *key, const GscVal *value,
                           GscVal *out) {
    GscNativeValue in = {0}, result = {0};
    int status;
    if (!o->is_entity || o->retired || !r->host.native_field)
        return 0;
    if (value) {
        if (value->kind == V_INT) {
            in.kind = GSC_NATIVE_INT;
            in.integer = value->i;
        } else if (value->kind == V_FLOAT) {
            in.kind = GSC_NATIVE_FLOAT;
            in.number = value->f;
        } else if (value->kind == V_STR || value->kind == V_ISTR) {
            in.kind = GSC_NATIVE_STRING;
            in.text = str_at(&r->prog, value->s);
        } else if(value->kind==V_ANIMATION || value->kind==V_ANIMTREE){
            in.kind=value->kind==V_ANIMATION?GSC_NATIVE_ANIMATION:GSC_NATIVE_ANIMTREE;
            in.tree=str_at(&r->prog,value->i);in.text=str_at(&r->prog,value->s);
        } else if (value->kind == V_VECTOR) {
            in.kind = GSC_NATIVE_VECTOR;
            memcpy(in.vector, value->vector, sizeof(in.vector));
        } else if (value->kind != V_UNDEF)
            return 0;
    }
    status = r->host.native_field(r->host.user, o->entity_id, key, value ? &in : NULL, &result);
    if (status < 0) {
        {char message[256];snprintf(message,sizeof(message),"GSC native field rejected: %s (entity %d)",key,o->entity_id);rt_error(r,message);}
        r->native_field_failure=1;
    }
    if (status > 0 && out)
        *out = rt_native_value(r, &result, 0);
    return status;
}
static void rt_set(GscRuntime *r, GscObj *o, const char *key, GscVal val) {
    if(o->deleted){rt_error(r,"GSC write to deleted entity");r->native_field_failure=1;return;}
    if(rt_native_field(r,o,key,&val,NULL))return;
    obj_set(o, key, val);
    if (val.kind == V_STR && (o == r->game.p || o == r->level.p) && r->host.set_team_names) {
        if (strcmp(key, "allies") == 0)
            r->host.set_team_names(r->host.user, str_at(&r->prog, val.s), NULL);
        if (strcmp(key, "axis") == 0)
            r->host.set_team_names(r->host.user, NULL, str_at(&r->prog, val.s));
    }
    if (val.kind == V_STR && o == r->level.p && strcmp(key, "gametype") == 0 &&
        r->host.set_gametype)
        r->host.set_gametype(r->host.user, str_at(&r->prog, val.s));
}
static GscVal rt_get(GscRuntime *r, GscObj *o, const char *key) {
    if(o->deleted)return make_undef();
    GscVal native=make_undef();if(rt_native_field(r,o,key,NULL,&native))return native;
    int n = obj_find(o, key);
    if (n >= 0)
        return o->fields[n].val;
    if (o->is_entity && !o->retired && o->entity_id != -1 && o->entity_id != -2 && r->host.entity_key) {
        const char *s = r->host.entity_key(r->host.user, o->entity_id, key);
        if (s && *s)
            return rt_string(r, s);
    }
    return make_undef();
}
/* The bridge serializes values rather than exposing arena-owned objects. */

static int rt_find_entities(GscRuntime* r,const char* key,const char* value,int* ids,int capacity) {
    int count=0,n=0,i,j;int native[4096];
    if(r->host.entity_find)n=r->host.entity_find(r->host.user,key,value,native,4096);
    if(n<0)n=0;if(n>4096)n=4096;
    for(i=0;i<n && count<capacity;i++){
        GscVal object=rt_entity(r,native[i]);GscObj* o=(GscObj*)object.p;
        int field=key[0]?obj_find(o,key):-1;
        if(field>=0){GscVal current=o->fields[field].val;if(current.kind!=V_STR || strcmp(str_at(&r->prog,current.s),value))continue;}
        ids[count++]=native[i];
    }
    for(i=0;i<r->arena.n && count<capacity;i++)if(r->arena.kinds[i]==1){
        GscObj* o=(GscObj*)r->arena.items[i];int field;
        if(!o->is_entity || o->deleted || o->entity_id==-1 || o->entity_id==-2)continue;
        {int c=obj_find(o,"classname");const char* cls=c>=0 && o->fields[c].val.kind==V_STR?str_at(&r->prog,o->fields[c].val.s):r->host.entity_key?r->host.entity_key(r->host.user,o->entity_id,"classname"):"";if(!cls || !*cls)continue;}
        if(key[0]){field=obj_find(o,key);if(field<0 || o->fields[field].val.kind!=V_STR || strcmp(str_at(&r->prog,o->fields[field].val.s),value))continue;}
        for(j=0;j<count;j++)if(ids[j]==o->entity_id)break;
        if(j==count)ids[count++]=o->entity_id;
    }
    return count;
}

static int rt_invoke_native(GscRuntime* r,const char* name,const GscNativeBinding* binding,
                            int receiver,int entity,GscVal* args,int argc,GscVal* out){
    GscNativeValue in[GSC_ARGUMENT_MAX],result;int i,status;
    if(receiver && (name_equal(name,"getEntityNumber") || name_equal(name,"getGuid"))){
        GscVal value=rt_entity(r,entity);
        if(value.kind==V_OBJECT && ((GscObj*)value.p)->retired){
            GscObj* object=(GscObj*)value.p;int field=obj_find(object,name_equal(name,"getGuid")?"_retired_guid":"_retired_number");
            if(argc || field<0)return -1;
            *out=object->fields[field].val;return 1;
        }
    }
    if(!binding && !r->host.native_call)return 0;
    memset(in,0,sizeof(in));memset(&result,0,sizeof(result));
    for(i=0;i<argc;i++) {
        GscVal v=args[i];
        if(v.kind==V_INT){in[i].kind=GSC_NATIVE_INT;in[i].integer=v.i;}
        else if(v.kind==V_FLOAT){in[i].kind=GSC_NATIVE_FLOAT;in[i].number=v.f;}
        else if(v.kind==V_STR||v.kind==V_ISTR){in[i].kind=GSC_NATIVE_STRING;in[i].text=str_at(&r->prog,v.s);}
        else if(v.kind==V_ANIMATION || v.kind==V_ANIMTREE){
            in[i].kind=v.kind==V_ANIMATION?GSC_NATIVE_ANIMATION:GSC_NATIVE_ANIMTREE;
            in[i].tree=str_at(&r->prog,v.i);in[i].text=str_at(&r->prog,v.s);
        }
        else if(v.kind==V_VECTOR){in[i].kind=GSC_NATIVE_VECTOR;memcpy(in[i].vector,v.vector,sizeof(v.vector));}
        else if(rt_id(v,&in[i].integer))in[i].kind=GSC_NATIVE_ENTITY;
        else if(v.kind!=V_UNDEF)return -1;
    }
    status=binding?binding->call(binding->user,receiver,entity,in,argc,&result):
        r->host.native_call(r->host.user,name,receiver,entity,in,argc,&result);
    if(status<=0)return status;
    switch(result.kind) {
    case GSC_NATIVE_ANIMATION:case GSC_NATIVE_ANIMTREE:
    case GSC_NATIVE_RECORD:*out=rt_native_value(r,&result,0);if(out->kind==V_UNDEF)return -1;break;
    case GSC_NATIVE_UNDEFINED:*out=make_undef();break;
    case GSC_NATIVE_INT:*out=make_int(result.integer);break;
    case GSC_NATIVE_FLOAT:if(!isfinite(result.number))return -1;*out=make_float(result.number);break;
    case GSC_NATIVE_STRING:*out=rt_string(r,result.text);break;
    case GSC_NATIVE_ENTITY:*out=rt_entity(r,result.integer);break;
    case GSC_NATIVE_VECTOR:
        *out=make_undef();out->kind=V_VECTOR;
        for(i=0;i<3;i++){if(!isfinite(result.vector[i]))return -1;out->vector[i]=result.vector[i];}
        break;
    default:return -1;
    }
    return 1;
}

static int rt_native(GscRuntime *r, const char *name, int receiver, int entity,
                     GscVal *args, int argc, GscVal *out) {
    int i;
    double a = 0, b = 0;
    if (!receiver && (name_equal(name,"int") || name_equal(name,"float"))) {
        double value; char *end;
        if(argc!=1)return -1;
        if(args[0].kind==V_INT)value=args[0].i;
        else if(args[0].kind==V_FLOAT)value=args[0].f;
        else if(args[0].kind==V_STR) {
            const char* text=str_at(&r->prog,args[0].s);
            value=strtod(text,&end);if(end==text)return -1;
            while(isspace((unsigned char)*end))++end;if(*end)return -1;
        }else return -1;
        if(!isfinite(value))return -1;
        if(name_equal(name,"int")) {
            if(value<INT_MIN||value>=(double)INT_MAX+1.0)return -1;
            *out=make_int((int)value);
        }else *out=make_float(value);
        return 1;
    }
    if (!receiver && (name_equal(name,"randomInt") || name_equal(name,"randomFloat"))) {
        uint32_t value,limit=0;
        if(argc!=1 || (args[0].kind!=V_INT && args[0].kind!=V_FLOAT))return -1;
        a=args[0].kind==V_INT?args[0].i:args[0].f;
        if(!isfinite(a)||a<=0)return -1;
        if(name_equal(name,"randomInt")){if(a<1 || a>INT_MAX)return -1;limit=(uint32_t)a;}
        do {
            value=r->random_state;
            value^=value<<13;value^=value>>17;value^=value<<5;
            r->random_state=value;
        } while(name_equal(name,"randomInt") && value < (uint32_t)(-limit) % limit);
        *out=name_equal(name,"randomInt")?make_int((int)(value%limit)):
            make_float((double)value*(1.0/4294967296.0)*a);
        return 1;
    }
    if (!receiver && name_equal(name, "getTime")) {
        if (argc) return -1;
        *out = make_int(r->now); return 1;
    }
    if (!receiver && (name_equal(name,"distance") || name_equal(name,"length") ||
        name_equal(name,"vectorNormalize") || name_equal(name,"vectorToAngles") ||
        name_equal(name,"anglesToForward"))) {
        double v[3], length;
        int distance = name_equal(name,"distance");
        if (argc != (distance ? 2 : 1) || args[0].kind != V_VECTOR ||
            (distance && args[1].kind != V_VECTOR)) return -1;
        for(i=0;i<3;i++) v[i]=args[0].vector[i]-(distance?args[1].vector[i]:0);
        length = sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if (distance || name_equal(name,"length")) *out=make_float(length);
        else {
            *out=make_undef();out->kind=V_VECTOR;
            if(name_equal(name,"vectorNormalize")) {
                for(i=0;i<3;i++)out->vector[i]=length?v[i]/length:0;
            } else if(name_equal(name,"anglesToForward")) {
                double pitch=v[0]*0.017453292519943295, yaw=v[1]*0.017453292519943295;
                out->vector[0]=cos(pitch)*cos(yaw);out->vector[1]=cos(pitch)*sin(yaw);
                out->vector[2]=-sin(pitch);
            } else {
                double yaw=atan2(v[1],v[0])*57.29577951308232;
                double pitch=-atan2(v[2],sqrt(v[0]*v[0]+v[1]*v[1]))*57.29577951308232;
                out->vector[0]=pitch<0?pitch+360:pitch;
                out->vector[1]=yaw<0?yaw+360:yaw;
            }
        }
        return 1;
    }
    if (!receiver && (name_equal(name,"abs") || name_equal(name,"sqrt") ||
                      name_equal(name,"min") || name_equal(name,"max"))) {
        int pair=name_equal(name,"min")||name_equal(name,"max");
        if(argc!=(pair?2:1))return -1;
        for(i=0;i<argc;i++)if(args[i].kind!=V_INT&&args[i].kind!=V_FLOAT)return -1;
        a=args[0].kind==V_INT?args[0].i:args[0].f;
        if(pair)b=args[1].kind==V_INT?args[1].i:args[1].f;
        if(name_equal(name,"sqrt")&&a<0)return -1;
        *out=make_float(name_equal(name,"abs")?fabs(a):name_equal(name,"sqrt")?sqrt(a):
                        name_equal(name,"min")?(a<b?a:b):(a>b?a:b));
        return 1;
    }
    return rt_invoke_native(r,name,NULL,receiver,entity,args,argc,out);
}

static void rt_run(GscRuntime *r, RtThread *t, int slice);
static void rt_method(GscRuntime *r, RtThread *t, const char* method, GscVal receiver, GscVal *args,
                      int argc, GscVal *result) {
    int id;
    if (name_equal(method, "notify") || name_equal(method, "waittill") ||
        name_equal(method, "waittillmatch") || name_equal(method, "endon")) {
        int i;
        if (receiver.kind != V_OBJECT || !receiver.p) {
            rt_error(r, "GSC event requires an object receiver");
            t->state = 3;
            return;
        }
        if (argc < 1 || args[0].kind != V_STR) {
            rt_error(r, "GSC event name must be string");
            t->state = 3;
            return;
        }
        if (name_equal(method, "notify"))
            rt_notify_values(r, receiver, args[0].s, args + 1, argc - 1, t);
        else if (name_equal(method, "waittill") || name_equal(method, "waittillmatch")) {
            t->state = 2;
            t->event_receiver = receiver;
            t->event_name = args[0].s;
            t->match_count = name_equal(method, "waittillmatch") ? argc - 1 : 0;
            for (i = 0; i < t->match_count; i++)
                t->match[i] = args[i + 1];
        } else if (t->nendon < RT_EVENTS) {
            t->endon[t->nendon].receiver = receiver;
            t->endon[t->nendon++].name = args[0].s;
        } else {
            rt_error(r, "GSC endon limit");
            t->state = 3;
        }
        return;
    }
    /* Cleanup can race another script across a bounded instruction slice.
     * A retired HUD must not destroy a newly reused native handle. */
    if(name_equal(method,"destroy") && receiver.kind==V_OBJECT && receiver.p &&
       ((GscObj*)receiver.p)->is_entity && ((GscObj*)receiver.p)->deleted)return;
    if (!rt_id(receiver, &id)) {
        rt_error(r, "GSC method requires entity receiver");
        t->state = 3;
        return;
    }
    {
        int status = rt_native(r, method, 1, id, args, argc, result);
        if (status) {
            if(status>0 && (name_equal(method,"delete") || name_equal(method,"destroy")))((GscObj*)receiver.p)->deleted=1;
            if (status < 0) { char message[256];snprintf(message,sizeof(message),"GSC native method rejected: %s (entity %d, args %d)",method,id,argc);rt_error(r,message); t->state = 3; }
            return;
        }
    }
    if (name_equal(method, "placeSpawnpoint") || name_equal(method, "placeSpawnPoint") ||
        name_equal(method, "placespawnpoint")) {
        if (argc || id < 0 || !r->host.place_spawnpoint ||
            !r->host.place_spawnpoint(r->host.user, id))
            rt_error(r, "GSC spawnpoint placement rejected");
    } else if (name_equal(method, "hide") || name_equal(method, "show") ||
               name_equal(method, "delete") || name_equal(method, "notsolid") ||
               name_equal(method, "solid") ||
               name_equal(method, "unlinkFromWorld") || name_equal(method, "linkIntoWorld")) {
        int visible = -1, linked = -1;
        if (name_equal(method, "hide"))
            visible = 0;
        if (name_equal(method, "show"))
            visible = 1;
        if (name_equal(method, "unlinkFromWorld"))
            linked = 0;
        if (name_equal(method, "linkIntoWorld"))
            linked = 1;
        if (name_equal(method, "delete")) {
            visible = 0;
            linked = 0;
        }
        if (name_equal(method, "notsolid"))
            linked = 0;
        if (name_equal(method, "solid"))
            linked = 1;
        if (id < 0 || !r->host.entity_state ||
            !r->host.entity_state(r->host.user, id, visible, linked))
            rt_error(r, "GSC entity state rejected");
    } else {
        char message[160];
        snprintf(message, sizeof(message), "GSC unsupported entity method: %s", method);
        rt_error(r, message);
        t->state = 3;
    }
}

static int rt_extended(GscRuntime *r, RtThread *t, const GscIns *ins) {
    GscVal a, b, out = make_undef();
    GscVal *stack = t->stack;
    int *depth = &t->sp;
    switch (ins->op) {
    case GSC_NATIVE_SLOT:
    case GSC_INTRINSIC_CALL:
    case GSC_INTRINSIC_METHOD: {
        GscVal args[GSC_ARGUMENT_MAX],receiver=make_undef();
        int i,entity=0,status,argc=ins->b,method=ins->op==GSC_INTRINSIC_METHOD || (ins->op==GSC_NATIVE_SLOT && ins->str);
        if(argc<0 || argc>GSC_ARGUMENT_MAX || *depth<argc+method)goto underflow;
        for(i=argc-1;i>=0;--i)args[i]=stack[--*depth];
        if(method)receiver=stack[--*depth];
        if(ins->op==GSC_INTRINSIC_METHOD){
            rt_method(r,t,rt_event_methods[ins->a],receiver,args,argc,&out);stack[(*depth)++]=out;return 1;
        }
        if(method && ins->op==GSC_NATIVE_SLOT && name_equal(r->bindings[ins->a].name,"destroy") &&
           receiver.kind==V_OBJECT && receiver.p && ((GscObj*)receiver.p)->is_entity && ((GscObj*)receiver.p)->deleted){stack[(*depth)++]=out;return 1;}
        if(method && !rt_id(receiver,&entity)){char message[256];snprintf(message,sizeof(message),"GSC native method requires entity receiver: %s at line %d",r->bindings[ins->a].name,ins->line);rt_error(r,message);t->state=3;return 1;}
        if(ins->op==GSC_NATIVE_SLOT){
            const GscNativeBinding* binding=&r->bindings[ins->a];
            status=rt_invoke_native(r,binding->name,binding,method,entity,args,argc,&out);
            if(status>0 && method && (name_equal(binding->name,"delete") || name_equal(binding->name,"destroy")))((GscObj*)receiver.p)->deleted=1;
        }else status=rt_native(r,rt_intrinsics[ins->a],0,0,args,argc,&out);
        if(status<=0){
            char message[256];const char* name=ins->op==GSC_NATIVE_SLOT?r->bindings[ins->a].name:rt_intrinsics[ins->a];
            snprintf(message,sizeof(message),"GSC bound native/intrinsic rejected: %s",name);
            rt_error(r,message);t->state=3;return 1;
        }
        stack[(*depth)++]=out;return 1;
    }
    case GSC_NOP:
    case GSC_IMPORT:
        return 1;
    case GSC_FRAME_END:
        t->state = 4;
        return 1;
    case GSC_POP:
        if (*depth < 1)
            goto underflow;
        --*depth;
        return 1;
    case GSC_DUP:
        if (*depth < 1)
            goto underflow;
        stack[*depth] = stack[*depth - 1];
        ++*depth;
        return 1;
    case GSC_DUP2:
        if (*depth < 2)
            goto underflow;
        stack[*depth] = stack[*depth - 2];
        stack[*depth + 1] = stack[*depth - 1];
        *depth += 2;
        return 1;
    case GSC_NAMED_CALL: {
        GscVal args[GSC_ARGUMENT_MAX];
        int i, status, argc = ins->b;
        if (ins->str || argc < 0 || argc > GSC_ARGUMENT_MAX || *depth < argc) {
            rt_error(r, "GSC invalid native invocation"); t->state = 3; return 1;
        }
        for (i = argc - 1; i >= 0; --i) args[i] = stack[--*depth];
        status = rt_native(r, str_at(&r->prog, ins->a), 0, 0, args, argc, &out);
        if (status > 0) { stack[(*depth)++] = out; return 1; }
        { char message[256];
          snprintf(message, sizeof(message), "GSC native %s: %s",
                   status < 0 ? "rejected" : "unbound", str_at(&r->prog, ins->a));
          rt_error(r, message); t->state = 3; return 1; }
    }
    case GSC_NAMED_REF: {
        char message[256];
        snprintf(message, sizeof(message), "GSC unresolved %s: %s",
                 ins->op == GSC_NAMED_REF ? "function reference" : "function",
                 str_at(&r->prog, ins->a));
        rt_error(r, message);
        t->state = 3;
        return 1;
    }
    case GSC_ANIMATION:
    case GSC_ANIMTREE:
        out.kind = ins->op == GSC_ANIMATION ? V_ANIMATION : V_ANIMTREE;
        out.i = ins->a;
        out.s = ins->op == GSC_ANIMATION ? ins->b : ins->a;
        stack[(*depth)++] = out;
        return 1;
    case GSC_BITNOT:
        if (*depth < 1)
            goto underflow;
        a = stack[*depth - 1];
        if (a.kind != V_INT)
            goto integer_type;
        stack[*depth - 1] = make_int(~a.i);
        return 1;
    case GSC_MOD:
    case GSC_SHL:
    case GSC_SHR:
        if (*depth < 2)
            goto underflow;
        b = stack[--*depth];
        a = stack[--*depth];
        if (a.kind != V_INT || b.kind != V_INT)
            goto integer_type;
        if (ins->op == GSC_MOD) {
            if (!b.i) {
                rt_error(r, "GSC remainder by zero");
                t->state = 3;
                return 1;
            }
            out = make_int(a.i == INT_MIN && b.i == -1 ? 0 : a.i % b.i);
        } else {
            uint32_t bits = (uint32_t)a.i;
            int value;
            if (b.i < 0 || b.i > 31) {
                rt_error(r, "GSC shift count outside 0..31");
                t->state = 3;
                return 1;
            }
            if (ins->op == GSC_SHL)
                bits <<= (unsigned)b.i;
            else if (b.i) {
                bits >>= (unsigned)b.i;
                if (a.i < 0)
                    bits |= UINT32_MAX << (32u - (unsigned)b.i);
            }
            memcpy(&value, &bits, sizeof(value));
            out = make_int(value);
        }
        stack[(*depth)++] = out;
        return 1;
    case GSC_LE:
    case GSC_GE:
        if (*depth < 2)
            goto underflow;
        b = stack[--*depth];
        a = stack[--*depth];
        if ((a.kind != V_INT && a.kind != V_FLOAT) || (b.kind != V_INT && b.kind != V_FLOAT)) {
            rt_error(r, "GSC relational operands must be numbers");
            t->state = 3;
            return 1;
        }
        {
            double left = a.kind == V_INT ? a.i : a.f, right = b.kind == V_INT ? b.i : b.f;
            out = make_int(ins->op == GSC_LE ? left <= right : left >= right);
        }
        stack[(*depth)++] = out;
        return 1;
    case GSC_CAST_INT:
    case GSC_CAST_FLOAT:
    case GSC_CAST_STRING:
    case GSC_CAST_BOOL:
        if (*depth < 1)
            goto underflow;
        a = stack[*depth - 1];
        if (ins->op == GSC_CAST_BOOL)
            out = make_int(val_truthy(&a));
        else if (ins->op == GSC_CAST_STRING) {
            char formatted[160];
            const char *s = NULL;
            if (a.kind == V_STR)
                s = str_at(&r->prog, a.s);
            else if (a.kind == V_INT) {
                snprintf(formatted, sizeof(formatted), "%d", a.i);
                s = formatted;
            } else if (a.kind == V_FLOAT) {
                snprintf(formatted, sizeof(formatted), "%.9g", a.f);
                s = formatted;
            } else if (a.kind == V_VECTOR) {
                snprintf(formatted, sizeof(formatted), "(%.9g, %.9g, %.9g)", a.vector[0],
                         a.vector[1], a.vector[2]);
                s = formatted;
            }
            if (s)
                out = rt_string(r, s);
        } else {
            double number = 0;
            int valid = 1;
            if (a.kind == V_INT)
                number = a.i;
            else if (a.kind == V_FLOAT)
                number = a.f;
            else if (a.kind == V_STR) {
                char *end;
                const char *s = str_at(&r->prog, a.s);
                number = strtod(s, &end);
                if (end == s)
                    valid = 0;
                while (isspace((unsigned char)*end))
                    end++;
                if (*end)
                    valid = 0;
            } else
                valid = 0;
            if (valid && isfinite(number)) {
                if (ins->op == GSC_CAST_FLOAT)
                    out = make_float(number);
                else if (number >= (double)INT_MIN && number < (double)INT_MAX + 1.0)
                    out = make_int((int)number);
            }
        }
        if (out.kind == V_UNDEF) {
            rt_error(r, "GSC invalid cast or numeric conversion range");
            t->state = 3;
            return 1;
        }
        stack[*depth - 1] = out;
        return 1;
    default:
        return 0;
    }
underflow:
    rt_error(r, "GSC stack underflow");
    t->state = 3;
    return 1;
integer_type:
    rt_error(r, "GSC integer operands required");
    t->state = 3;
    return 1;
}

static void rt_run(GscRuntime *r, RtThread *t, int slice) {
    GscProg *prog = &r->prog;
    GscVal *stack = t->stack;
    GscFrame *frames = t->frames;
    struct Resume {
        RtThread *thread;
        int slice;
    } resumes[RT_THREADS];
    int nresumes = 0;
    RtThread *pending_child = NULL;
    /* Macros alias the persistent stack pointers across child thread execution. */
#define sp (t->sp)
#define fp (t->fp)
    for (;;) {
        while (t->state == 0 && slice-- > 0 && r->budget > 0) {
            GscIns instruction;
            const GscIns *ins;
            if (!fp || t->ip < 0 || t->ip >= prog->ncode) {
                t->state = 3;
                break;
            }
            if (sp >= GSC_STACK_MAX - 32) {
                rt_error(r, "GSC stack limit");
                t->state = 3;
                break;
            }
            r->budget--;
            instruction = prog->code[t->ip++];
            ins = &instruction;
            if (ins->op >= GSC_NOP && rt_extended(r, t, ins))
                continue;
            switch (ins->op) {
            case GSC_PUSH_UNDEF:
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_undef();
                break;
            case GSC_PUSH_INT:
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_int(ins->a);
                break;
            case GSC_BITAND:
            case GSC_BITOR:
            case GSC_BITXOR: {
                GscVal right = sp ? stack[--sp] : make_undef();
                GscVal left = sp ? stack[--sp] : make_undef();
                if (left.kind != V_INT || right.kind != V_INT) {
                    rt_error(r, "GSC bitwise operands must be integers");
                    t->state = 3;
                    break;
                }
                stack[sp++] = make_int(ins->op == GSC_BITAND  ? left.i & right.i
                                       : ins->op == GSC_BITOR ? left.i | right.i
                                                              : left.i ^ right.i);
                break;
            }
            case GSC_NEG:
                if (sp > 0)
                    stack[sp - 1] = val_neg(&stack[sp - 1]);
                break;
            case GSC_VECTOR: {
                GscVal out = make_undef();
                int i;
                if (sp < 3) {
                    rt_error(r, "GSC vector stack underflow");
                    t->state = 3;
                    break;
                }
                out.kind = V_VECTOR;
                for (i = 2; i >= 0; i--) {
                    GscVal value = stack[--sp];
                    if (value.kind == V_INT)
                        out.vector[i] = value.i;
                    else if (value.kind == V_FLOAT)
                        out.vector[i] = value.f;
                    else
                        out.kind = V_UNDEF;
                }
                if (out.kind == V_UNDEF) {
                    rt_error(r, "GSC vector components must be numeric");
                    t->state = 3;
                } else
                    stack[sp++] = out;
                break;
            }
            case GSC_PUSH_FLOAT:
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_float(ins->f);
                break;
            case GSC_PUSH_STR:
            case GSC_PUSH_ISTR: {
                GscVal v = make_undef();
                v.kind = ins->op == GSC_PUSH_ISTR ? V_ISTR : V_STR;
                v.s = ins->str;
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = v;
                break;
            }
            case GSC_GET_LOCAL: {
                GscFrame *f = &frames[fp - 1];
                if (ins->a >= 0 && ins->a < f->nlocals) {
                    if (ins->b && f->locals[ins->a].kind == V_UNDEF) {
                        f->locals[ins->a] = arena_array_value(&r->arena);
                        if (f->locals[ins->a].kind == V_UNDEF) {
                            rt_error(r, "GSC array allocation limit");
                            t->state = 3;
                            break;
                        }
                    }
                    if (sp < GSC_STACK_MAX)
                        stack[sp++] = f->locals[ins->a];
                } else if (sp < GSC_STACK_MAX) {
                    stack[sp++] = make_undef();
                }
                break;
            }
            case GSC_SET_LOCAL: {
                GscFrame *f = &frames[fp - 1];
                GscVal v = (sp > 0) ? stack[--sp] : make_undef();
                if (ins->a >= 0 && ins->a < f->nlocals)
                    f->locals[ins->a] = v;
                /* else: discard (scratch slot that was never allocated). */
                break;
            }
            case GSC_ADD: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = val_add(&a, &b, prog);
                break;
            }
            case GSC_SUB: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = val_sub(&a, &b);
                break;
            }
            case GSC_MUL: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = val_mul(&a, &b);
                break;
            }
            case GSC_DIV: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = val_div(&a, &b);
                break;
            }
            case GSC_LT: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_int(val_lt(&a, &b) ? 1 : 0);
                break;
            }
            case GSC_GT: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_int(val_gt(&a, &b) ? 1 : 0);
                break;
            }
            case GSC_EQ: {
                GscVal b = (sp > 0) ? stack[--sp] : make_undef();
                GscVal a = (sp > 0) ? stack[--sp] : make_undef();
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = make_int(val_eq(&a, &b) ? 1 : 0);
                break;
            }
            case GSC_JUMP:
                t->ip = ins->a;
                continue;
            case GSC_JUMP_BACK:
                /* Backward edge of a loop. Counted like any other instruction
                 * for fuel; distinct opcode so the compiler and reader can
                 * tell loop back edges apart from forward jumps. */
                t->ip = ins->a;
                continue;
            case GSC_JUMP_IF_FALSE: {
                GscVal v = (sp > 0) ? stack[--sp] : make_undef();
                if (!val_truthy(&v)) {
                    t->ip = ins->a;
                    continue;
                }
                break;
            }
            case GSC_NEW_ARRAY: {
                GscArr *a = arena_new_arr(&r->arena);
                if (!a) {
                    rt_error(r, "GSC array allocation limit");
                    t->state = 3;
                    break;
                }
                GscVal v = make_undef();
                v.kind = V_ARRAY;
                v.p = a;
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = v;
                break;
            }
            case GSC_GET_INDEX: {
                GscVal idx = (sp > 0) ? stack[--sp] : make_undef();
                GscVal base = (sp > 0) ? stack[--sp] : make_undef();
                GscVal out = make_undef();
                if (base.kind == V_VECTOR && idx.kind == V_INT && idx.i >= 0 && idx.i < 3) {
                    out = make_float(base.vector[idx.i]);
                } else if (base.kind == V_ARRAY && base.p && idx.kind == V_INT) {
                    GscArr *a = (GscArr *)base.p;
                    if (idx.i >= 0 && idx.i < a->size)
                        out = a->items[idx.i];
                } else if (base.kind == V_ARRAY && base.p && idx.kind == V_STR) {
                    out = arr_get_string((GscArr *)base.p, idx.s);
                } else if (base.kind == V_OBJECT && base.p && idx.kind == V_STR) {
                    out = rt_get(r, (GscObj *)base.p, str_at(prog, idx.s));
                }
                if (ins->b && out.kind == V_UNDEF && base.kind == V_ARRAY && base.p) {
                    out = arena_array_value(&r->arena);
                    if (out.kind == V_UNDEF) {
                        rt_error(r, "GSC array allocation limit");
                        t->state = 3;
                        break;
                    }
                    if (idx.kind == V_INT)
                        arr_set((GscArr *)base.p, idx.i, out);
                    else if (idx.kind == V_STR && !arr_set_string((GscArr *)base.p, idx.s, out)) {
                        rt_error(r, "GSC associative array allocation limit");
                        t->state = 3;
                        break;
                    }
                }
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = out;
                break;
            }
            case GSC_SET_INDEX: {
                GscVal val = (sp > 0) ? stack[--sp] : make_undef();
                GscVal idx = (sp > 0) ? stack[--sp] : make_undef();
                GscVal base = (sp > 0) ? stack[--sp] : make_undef();
                if (base.kind == V_ARRAY && base.p && idx.kind == V_INT) {
                    arr_set((GscArr *)base.p, idx.i, val);
                } else if (base.kind == V_ARRAY && base.p && idx.kind == V_STR) {
                    if (!arr_set_string((GscArr *)base.p, idx.s, val)) {
                        rt_error(r, "GSC associative array allocation limit");
                        t->state = 3;
                    }
                } else if (base.kind == V_OBJECT && base.p && idx.kind == V_STR) {
                    rt_set(r, (GscObj *)base.p, str_at(prog, idx.s), val);
                }
                break;
            }
            case GSC_GET_FIELD: {
                GscVal base = (sp > 0) ? stack[--sp] : make_undef();
                GscVal out = make_undef();
                const char *key = str_at(prog, ins->a);
                if (base.kind == V_ARRAY && key && strcmp(key, "size") == 0) {
                    GscArr *a = (GscArr *)base.p;
                    out = make_int(a ? a->count + a->nstrings : 0);
                } else if (base.kind == V_OBJECT && base.p) {
                    out = rt_get(r, (GscObj *)base.p, key);
                    if (ins->b && out.kind == V_UNDEF) {
                        out = arena_array_value(&r->arena);
                        if (out.kind == V_UNDEF) {
                            rt_error(r, "GSC array allocation limit");
                            t->state = 3;
                            break;
                        }
                        rt_set(r, (GscObj *)base.p, key, out);
                    }
                }
                if (sp < GSC_STACK_MAX)
                    stack[sp++] = out;
                break;
            }
            case GSC_SET_FIELD: {
                GscVal val = (sp > 0) ? stack[--sp] : make_undef();
                GscVal base = (sp > 0) ? stack[--sp] : make_undef();
                const char *key = str_at(prog, ins->a);
                if (base.kind == V_OBJECT && base.p) {
                    rt_set(r, (GscObj *)base.p, key, val);
                }
                break;
            }
            case GSC_NOT: {
                GscVal v = sp ? stack[--sp] : make_undef();
                stack[sp++] = make_int(!val_truthy(&v));
                break;
            }
            case GSC_FUNC_REF:
            case GSC_FAR_REF: {
                GscVal v = make_undef();
                int fn = ins->a, i, file = -1;
                if (ins->op == GSC_FAR_REF) {
                    for (i = 0; i < r->nfiles; i++)
                        if (strcmp(r->files[i].path, str_at(prog, ins->a)) == 0) {
                            file = i;
                            break;
                        }
                    fn = rt_function(r, file, str_at(prog, ins->b));
                }
                if (fn < 0 || fn >= prog->nfunc) {
                    rt_error(r, "GSC unresolved function reference");
                    t->state = 3;
                    break;
                }
                v.kind = V_FUNCTION;
                v.i = fn;
                stack[sp++] = v;
                break;
            }
            case GSC_INDIRECT: {
                GscVal args[GSC_ARGUMENT_MAX], ref, receiver = t->self;
                int argc = ins->b, i, entity = ins->a & 1;
                if (argc < 0 || argc > GSC_ARGUMENT_MAX || sp < argc + 1 + entity) {
                    rt_error(r, "GSC indirect argument limit");
                    t->state = 3;
                    break;
                }
                for (i = argc - 1; i >= 0; i--)
                    args[i] = stack[--sp];
                ref = stack[--sp];
                if (entity)
                    receiver = stack[--sp];
                if (ref.kind != V_FUNCTION) {
                    rt_error(r, "GSC indirect call requires function reference");
                    t->state = 3;
                    break;
                }
                if (ins->a & 2) {
                    pending_child = rt_spawn(r, ref.i, args, argc, receiver);
                    stack[sp++] = make_undef();
                } else
                    rt_enter(r, t, ref.i, args, argc, receiver);
                break;
            }
            case GSC_RETURN: {
                GscVal result = sp ? stack[--sp] : make_undef();
                GscFrame *f = &frames[fp - 1];
                int next = f->return_ip;
                sp = f->sp_base;
                t->self = f->saved_self;
                free(f->locals);
                f->locals = NULL;
                fp--;
                if (!fp) {
                    t->state = 3;
                    break;
                }
                t->ip = next;
                stack[sp++] = result;
                break;
            }
            case GSC_GET_GLOBAL:
                stack[sp++] = ins->a == 0   ? t->self
                              : ins->a == 1 ? r->level
                              : ins->a == 2 ? r->game
                                            : r->anim;
                break;
            case GSC_WAIT: {
                GscVal v = sp ? stack[--sp] : make_undef();
                double seconds = v.kind == V_INT ? v.i : v.kind == V_FLOAT ? v.f : 0;
                if (!(seconds >= 0) || seconds > 2147483) {
                    rt_error(r, "GSC invalid wait duration");
                    t->state = 3;
                    break;
                }
                t->wake = (int64_t)r->now + (int64_t)(seconds * 1000.0 + 0.999999);
                t->epoch = r->epoch + 1;
                t->state = 1;
                break;
            }
            case GSC_EVENT_ARG: {
                GscFrame *f = &frames[fp - 1];
                if (ins->a >= 0 && ins->a < f->nlocals)
                    f->locals[ins->a] =
                        ins->b < t->payload_count ? t->payload[ins->b] : make_undef();
                break;
            }
            case GSC_METHOD: {
                GscVal args[GSC_ARGUMENT_MAX], receiver, result = make_undef();
                int i, argc = ins->b;
                if (argc < 0 || argc > GSC_ARGUMENT_MAX || sp < argc + 1) {
                    rt_error(r, "GSC method argument limit");
                    t->state = 3;
                    break;
                }
                for (i = argc - 1; i >= 0; i--)
                    args[i] = stack[--sp];
                receiver = stack[--sp];
                rt_method(r, t, str_at(prog,ins->a), receiver, args, argc, &result);
                stack[sp++] = result;
                break;
            }
            case GSC_CALL:
            case GSC_FAR:
            case GSC_THREAD:
            case GSC_ENTITY_CALL:
            case GSC_ENTITY_FAR:
            case GSC_ENTITY_THREAD: {
                GscVal args[GSC_ARGUMENT_MAX], receiver = t->self;
                int i, fn = ins->a, argc = ins->b;
                int entity = ins->op == GSC_ENTITY_CALL || ins->op == GSC_ENTITY_FAR ||
                             ins->op == GSC_ENTITY_THREAD;
                int threaded = ins->op == GSC_THREAD || ins->op == GSC_ENTITY_THREAD;
                int far =
                    ins->op == GSC_FAR || ins->op == GSC_ENTITY_FAR || (threaded && ins->str >= 0);
                if (far) {
                    const char *path = str_at(prog, ins->a);
                    int file = -1;
                    argc = ins->str;
                    for (i = 0; i < r->nfiles; i++)
                        if (strcmp(r->files[i].path, path) == 0) {
                            file = i;
                            break;
                        }
                    fn = rt_function(r, file, str_at(prog, ins->b));
                }
                if (argc < 0 || argc > GSC_ARGUMENT_MAX || sp < argc + entity) {
                    rt_error(r, "GSC call argument limit");
                    t->state = 3;
                    break;
                }
                for (i = argc - 1; i >= 0; i--)
                    args[i] = stack[--sp];
                if (entity)
                    receiver = stack[--sp];
                if (threaded) {
                    pending_child = rt_spawn(r, fn, args, argc, receiver);
                    stack[sp++] = make_undef();
                } else
                    rt_enter(r, t, fn, args, argc, receiver);
                break;
            }
            case GSC_BUILTIN: {
                GscVal args[GSC_ARGUMENT_MAX], out = make_undef();
                int i, argc = ins->b, id = ins->a;
                if (argc < 0 || argc > GSC_ARGUMENT_MAX || sp < argc) {
                    rt_error(r, "GSC builtin argument limit");
                    t->state = 3;
                    break;
                }
                for (i = argc - 1; i >= 0; i--)
                    args[i] = stack[--sp];
                if (id == 0) {
                    char message[160];
                    snprintf(message, sizeof(message), "GSC unsupported builtin: %s",
                             str_at(prog, ins->str));
                    rt_error(r, message);
                }
                if (id == GSC_BU_STRUCT) {
                    out.kind = V_OBJECT;
                    out.p = arena_new_obj(&r->arena);
                    if (!out.p) {
                        rt_error(r, "GSC object allocation limit");
                        t->state = 3;
                    }
                } else if (id == GSC_BU_DEFINED)
                    out = make_int(argc && args[0].kind != V_UNDEF && !(args[0].kind==V_OBJECT && args[0].p && ((GscObj*)args[0].p)->deleted));
                else if (id == GSC_BU_GETENT || id == GSC_BU_GETENTARRAY) {
                    int ids[4096], count = 0;
                    GscArr *a = NULL;
                    if (id == GSC_BU_GETENTARRAY) {
                        out.kind = V_ARRAY;
                        out.p = a = arena_new_arr(&r->arena);
                        if (!a) {
                            rt_error(r, "GSC array allocation limit");
                            t->state = 3;
                            break;
                        }
                    }
                    if (argc && args[0].kind == V_STR && r->host.entity_find)
                        count = rt_find_entities(
                            r,
                            argc > 1 && args[1].kind == V_STR ? str_at(prog, args[1].s)
                                                              : "targetname",
                            str_at(prog, args[0].s), ids, id == GSC_BU_GETENT ? 1 : 4096);
                    else if (!argc && id == GSC_BU_GETENTARRAY && r->host.entity_find)
                        count = rt_find_entities(r, "", "", ids, 4096);
                    if (count > 4096)
                        count = 4096;
                    for (i = 0; i < count; i++) {
                        GscVal e = rt_entity(r, ids[i]);
                        if (a)
                            arr_set(a, i, e);
                        else {
                            out = e;
                            break;
                        }
                    }
                } else if (id == GSC_BU_GETCVAR || id == GSC_BU_GETCVARINT ||
                           id == GSC_BU_GETCVARFLOAT) {
                    const char *value = "";
                    if (argc && args[0].kind == V_STR && r->host.get_cvar)
                        value = r->host.get_cvar(r->host.user, str_at(prog, args[0].s));
                    out = id == GSC_BU_GETCVAR        ? rt_string(r, value)
                          : id == GSC_BU_GETCVARFLOAT ? make_float(value ? atof(value) : 0)
                                                      : make_int(value ? atoi(value) : 0);
                } else {
                    builtin_call(&r->host, id, args, argc, prog);
                    if (id == GSC_BU_LOAD_FX && argc)
                        out = args[0];
                }
                if (id == GSC_BU_STRUCT || id == GSC_BU_LOAD_FX || id >= GSC_BU_GETENT)
                    stack[sp++] = out;
                break;
            }
            case GSC_STOP:
                t->state = 3;
                break;
            case GSC_DIAGNOSTIC:
                rt_error(r, str_at(prog, ins->str));
                break;
            default:
                rt_error(r, "GSC invalid opcode");
                t->state = 3;
                break;
            }
            if(r->native_field_failure){r->native_field_failure=0;t->state=3;}
            if (pending_child) {
                RtThread *child = pending_child;
                pending_child = NULL;
                if (child->state == 0 && nresumes < RT_THREADS) {
                    resumes[nresumes].thread = t;
                    resumes[nresumes++].slice = slice;
                    t = child;
                    stack = t->stack;
                    frames = t->frames;
                    slice = r->loading ? r->budget : 1000;
                } else {
                    int index;
                    if (child->state == 0)
                        rt_error(r, "GSC inline thread nesting limit");
                    for (index = 0; index < RT_THREADS; index++)
                        if (r->threads[index] == child) {
                            r->threads[index] = NULL;
                            break;
                        }
                    rt_free_thread(child);
                }
            }
        }
        if (!nresumes)
            break;
        if (t->state == 0)
            t->epoch = r->epoch + 1;
        if (t->state == 3) {
            int index;
            for (index = 0; index < RT_THREADS; index++)
                if (r->threads[index] == t) {
                    r->threads[index] = NULL;
                    break;
                }
            rt_free_thread(t);
        }
        --nresumes;
        t = resumes[nresumes].thread;
        slice = resumes[nresumes].slice;
        stack = t->stack;
        frames = t->frames;
    }
#undef sp
#undef fp
}
GscRuntime* gsc_runtime_create(const GscHost* host) {
    GscRuntime* r=(GscRuntime*)calloc(1,sizeof(*r));if(!r)return NULL;
    r->arena.limit=8192;r->random_state=0x9e3779b9u;
    if(host)r->host=*host;
    r->level=rt_entity(r,-1);r->game=rt_entity(r,-2);
    r->anim=make_undef();r->anim.p=arena_new_obj(&r->arena);if(r->anim.p)r->anim.kind=V_OBJECT;
    if(!r->level.p || !r->game.p || !r->anim.p){gsc_runtime_destroy(r);return NULL;}return r;
}
GscRuntime* gsc_runtime_create_linked(const GscHost* host,const GscNativeBinding* bindings,int count){
    GscRuntime* r;int i,j;
    if(count<0 || count>1024 || (count && !bindings))return NULL;
    for(i=0;i<count;++i){
        if(!bindings[i].name || !bindings[i].name[0] || strlen(bindings[i].name)>127 || !bindings[i].call ||
           !bindings[i].flags || (bindings[i].flags&~(GSC_BIND_FUNCTION|GSC_BIND_METHOD)))return NULL;
        for(j=0;j<i;++j)if(name_equal(bindings[i].name,bindings[j].name) && (bindings[i].flags&bindings[j].flags))return NULL;
    }
    r=gsc_runtime_create(host);if(!r)return NULL;r->closed_link=1;
    if(count){r->bindings=(GscNativeBinding*)calloc((size_t)count,sizeof(*r->bindings));if(!r->bindings){gsc_runtime_destroy(r);return NULL;}}
    for(i=0;i<count;++i){
        char* name=(char*)malloc(strlen(bindings[i].name)+1);if(!name){gsc_runtime_destroy(r);return NULL;}
        strcpy(name,bindings[i].name);r->bindings[i]=bindings[i];r->bindings[i].name=name;++r->nbindings;
    }
    return r;
}
void gsc_runtime_destroy(GscRuntime* r) {
    int i;if(!r)return;for(i=0;i<RT_THREADS;i++)rt_free_thread(r->threads[i]);
    for(i=0;i<r->nfiles;i++)free(r->files[i].imports);
    for(i=0;i<r->nbindings;++i)free((void*)r->bindings[i].name);
    free(r->bindings);
    arena_free(&r->arena);gsc_prog_free(&r->prog);free(r);
}
int gsc_runtime_start(GscRuntime* r,const char* path,const char* source,size_t size,const char* function,int self) {
    int file,fn;if(!r || !path || !function || r->running)return 0;
    if(r->closed_link && (!r->sealed || r->link_failed)){rt_error(r,"GSC program must link before execution");return 0;}
    file=rt_load(r,path,source,size);rt_link(r);fn=rt_function(r,file,function);
    if(fn<0)return 0;
    {RtThread* t=rt_spawn(r,fn,NULL,0,rt_entity(r,self));if(t && name_equal(function,"main"))t->main_root=1;return t!=NULL;}
}

static GscVal rt_host_argument(GscRuntime* r,const GscArgument* a) {
    GscVal v=a->kind==1?make_int(a->integer):a->kind==2?make_float(a->number):a->kind==3?rt_string(r,a->text):a->kind==4?rt_entity(r,a->integer):make_undef();
    if(a->kind==5){v.kind=V_VECTOR;memcpy(v.vector,a->vector,sizeof(v.vector));}
    return v;
}

int gsc_runtime_reset_map(GscRuntime* r) {
    int i;if(!r || r->running)return 0;
    for(i=0;i<RT_THREADS;i++){rt_free_thread(r->threads[i]);r->threads[i]=NULL;}
    for(i=0;i<r->arena.n;i++)if(r->arena.kinds[i]==1){
        GscObj* object=(GscObj*)r->arena.items[i];
        if(object->retired){object->deleted=1;continue;}
        if(!object->is_entity || object->deleted || object->entity_id==-2)continue;
        if(object->entity_id<=-3){
            int index=obj_find(object,"pers");GscVal pers=index>=0?object->fields[index].val:make_undef();
            free(object->fields);object->fields=NULL;object->size=object->cap=0;
            if(pers.kind!=V_UNDEF)obj_set(object,"pers",pers);
        }else object->deleted=1;
    }
    r->level=rt_entity(r,-1);r->start_requested=r->start_dispatched=r->start_complete=0;r->start_function=-1;
    r->now=0;r->epoch=r->cursor=0;r->native_field_failure=0;rt_collect(r);return r->level.kind==V_OBJECT;
}

int gsc_runtime_call_level(GscRuntime* r,const char* field,int self,const GscArgument* args,int argc) {
    GscVal ref,values[GSC_ARGUMENT_MAX];int i;
    if(!r || !field || argc<0 || argc>GSC_ARGUMENT_MAX || (argc && !args))return 0;
    ref=rt_get(r,(GscObj*)r->level.p,field);
    if(ref.kind!=V_FUNCTION || ref.i<0 || ref.i>=r->prog.nfunc)return 0;
    for(i=0;i<argc;i++)values[i]=rt_host_argument(r,&args[i]);
    return rt_spawn(r,ref.i,values,argc,rt_entity(r,self))!=NULL;
}
static int rt_copy_argument(GscRuntime* r,GscVal v,GscArgument* out){
    memset(out,0,sizeof(*out));
    if(v.kind==V_INT){out->kind=1;out->integer=v.i;}
    else if(v.kind==V_FLOAT){out->kind=2;out->number=v.f;}
    else if(v.kind==V_STR || v.kind==V_ISTR){out->kind=3;out->text=str_at(&r->prog,v.s);}
    else if(v.kind==V_VECTOR){out->kind=5;memcpy(out->vector,v.vector,sizeof(out->vector));}
    else if(v.kind==V_OBJECT && v.p && ((GscObj*)v.p)->is_entity && !((GscObj*)v.p)->deleted){out->kind=4;out->integer=((GscObj*)v.p)->entity_id;}
    return out->kind!=0;
}
int gsc_runtime_find_entities(GscRuntime* r,const char* key,const char* value,int* ids,int capacity){
    if(!r || r->running || !key || !value || capacity<0 || capacity>4096 || (capacity && !ids))return 0;
    return rt_find_entities(r,key,value,ids,capacity);
}
int gsc_runtime_get_entity_index(GscRuntime* r,int entity,const char* field,int index,GscArgument* out){
    GscVal object,array;if(!r || !field || !out || index<0 || index>65535)return 0;
    memset(out,0,sizeof(*out));
    object=rt_entity(r,entity);if(object.kind!=V_OBJECT)return 0;
    array=rt_get(r,(GscObj*)object.p,field);if(array.kind!=V_ARRAY)return 0;
    if(index>=((GscArr*)array.p)->size)return 0;
    return rt_copy_argument(r,((GscArr*)array.p)->items[index],out);
}
int gsc_runtime_get_entity_value(GscRuntime* r,int entity,const char* field,const char* key,GscArgument* out) {
    GscVal object,v;if(!r || !field || !out)return 0;memset(out,0,sizeof(*out));
    object=rt_entity(r,entity);if(object.kind!=V_OBJECT)return 0;
    v=rt_get(r,(GscObj*)object.p,field);
    if(key){if(v.kind!=V_ARRAY)return 0;v=arr_get_string((GscArr*)v.p,prog_intern(&r->prog,key));}
    return rt_copy_argument(r,v,out);
}
void gsc_runtime_cancel_entity_threads(GscRuntime* r,int entity){
    int i,j;if(!r)return;
    for(i=0;i<RT_THREADS;i++){
        RtThread* t=r->threads[i];if(!t || t->state==3)continue;
        if(t->self.kind==V_OBJECT && ((GscObj*)t->self.p)->entity_id==entity)t->state=3;
        if(t->state==2 && t->event_receiver.kind==V_OBJECT && ((GscObj*)t->event_receiver.p)->entity_id==entity)t->state=3;
        for(j=0;j<t->fp;j++)if(t->frames[j].saved_self.kind==V_OBJECT && ((GscObj*)t->frames[j].saved_self.p)->entity_id==entity)t->state=3;
    }
}
int gsc_runtime_retire_entity(GscRuntime* r,int entity) {
    static const char* fields[]={"name","health","maxhealth","score","deaths","sessionteam","sessionstate","model","origin","angles"};
    GscVal value;GscObj* object;unsigned i;
    if(!r || entity>-3 || r->retire_serial>=100000000)return 0;
    value=rt_entity(r,entity);if(value.kind!=V_OBJECT)return 0;object=(GscObj*)value.p;
    for(i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
        GscNativeValue out={0};
        if(r->host.native_field && r->host.native_field(r->host.user,entity,fields[i],NULL,&out)>0)
            obj_set(object,fields[i],rt_native_value(r,&out,0));
    }
    obj_set(object,"_retired_number",make_int(-3-entity));
    {GscNativeValue out={0};if(r->host.native_call && r->host.native_call(r->host.user,"getGuid",1,entity,NULL,0,&out)>0)
        obj_set(object,"_retired_guid",rt_native_value(r,&out,0));else obj_set(object,"_retired_guid",make_int(0));}
    object->retired=1;object->entity_id=(-2147483647-1)+(++r->retire_serial);
    return object->entity_id;
}
int gsc_runtime_entity_busy(const GscRuntime* r,int entity) {
    int i,j;if(!r)return 0;
    for(i=0;i<RT_THREADS;i++){
        const RtThread* t=r->threads[i];if(!t || t->state==3)continue;
        if(t->self.kind==V_OBJECT && ((GscObj*)t->self.p)->entity_id==entity)return 1;
        if(t->state==2 && t->event_receiver.kind==V_OBJECT && ((GscObj*)t->event_receiver.p)->entity_id==entity)return 1;
        for(j=0;j<t->fp;j++)if(t->frames[j].saved_self.kind==V_OBJECT && ((GscObj*)t->frames[j].saved_self.p)->entity_id==entity)return 1;
    }
    return 0;
}

void gsc_runtime_forget_entity(GscRuntime* r,int entity) {
    GscVal object;int i,j;if(!r || entity==-1 || entity==-2)return;
    object=rt_entity(r,entity);if(object.kind!=V_OBJECT)return;
    for(i=0;i<RT_THREADS;i++) {
        RtThread* t=r->threads[i];if(!t)continue;
        if(t->self.p==object.p || (t->state==2 && t->event_receiver.p==object.p))t->state=3;
        for(j=0;j<t->nendon;j++)if(t->endon[j].receiver.p==object.p)t->state=3;
    }
    ((GscObj*)object.p)->deleted=1;
}

int gsc_runtime_start_gametype(GscRuntime* r) {
    int file,fn;
    if(!r || r->running)return 0;
    if(r->start_requested)return 1;
    file=rt_load(r,"maps/mp/gametypes/_callbacksetup.gsc",NULL,0);
    rt_link(r);
    fn=rt_function(r,file,"CodeCallback_StartGameType");
    if(fn<0){rt_error(r,"GSC missing CodeCallback_StartGameType");return 0;}
    r->start_function=fn;r->start_requested=1;return 1;
}
int gsc_runtime_tick(GscRuntime* r,int time_ms,int budget) {
    unsigned k;int used;if(!r || r->running || budget<=0)return 0;
    r->now=time_ms;r->epoch++;r->budget=budget;r->running=1;r->loading=budget>=200000;
    for(k=0;k<RT_THREADS && r->budget>0;k++) {
        unsigned index=(r->cursor+k)%RT_THREADS;RtThread* t=r->threads[index];
        if(!t)continue;
        if(t->state==1 && t->wake<=r->now && t->epoch<=r->epoch)t->state=0;
        if(t->state==0 && t->epoch<=r->epoch){rt_run(r,t,r->loading?r->budget:1000);if(t->state==0)t->epoch=r->epoch+1;}
    }
    r->cursor=(r->cursor+k)%RT_THREADS;
    for(k=0;k<RT_THREADS;k++)if(r->threads[k] && r->threads[k]->state==3){if(r->threads[k]->start_root)r->start_complete=1;rt_free_thread(r->threads[k]);r->threads[k]=NULL;}
    if(r->start_requested && !r->start_dispatched) {
        int i,pending=0;
        for(i=0;i<RT_THREADS;i++)if(r->threads[i] && r->threads[i]->main_root && r->threads[i]->state==0)pending=1;
        if(!pending) {
            RtThread* t;
            t=rt_spawn(r,r->start_function,NULL,0,r->level);
            if(t){r->start_dispatched=1;t->start_root=1;}
            if(t && t->state==0)rt_run(r,t,r->loading?r->budget:1000);
        }
    }
    {
        int progress;
        do {
            progress=0;
            for(k=0;k<RT_THREADS && r->budget>0;k++) {
                RtThread* t=r->threads[(r->cursor+k)%RT_THREADS];
                if(t && t->state==4 && t->frame_end_epoch!=r->epoch) {
                    t->frame_end_epoch=r->epoch;t->state=0;progress=1;
                    rt_run(r,t,r->loading?r->budget:1000);
                    if(t->state==0)t->epoch=r->epoch+1;
                }
            }
        } while(progress && r->budget>0);
    }
    for(k=0;k<RT_THREADS;k++)if(r->threads[k] && r->threads[k]->state==3){if(r->threads[k]->start_root)r->start_complete=1;rt_free_thread(r->threads[k]);r->threads[k]=NULL;}
    for(k=0;k<RT_THREADS;k++)if(r->threads[k] && r->threads[k]->start_root && r->threads[k]->state!=0)r->start_complete=1;
    rt_collect(r);
    used=budget-r->budget;r->running=0;return used;
}
int gsc_runtime_set_entity_value(GscRuntime* r,int entity,const char* field,const char* key,const GscArgument* value) {
    GscVal object,v,container;int errors;
    if(!r || !field || !*field || !value)return 0;
    object=rt_entity(r,entity);if(object.kind!=V_OBJECT)return 0;
    v=rt_host_argument(r,value);
    errors=r->errors;
    if(!key){rt_set(r,(GscObj*)object.p,field,v);return errors==r->errors;}
    container=rt_get(r,(GscObj*)object.p,field);
    if(container.kind==V_UNDEF){container.kind=V_ARRAY;container.p=arena_new_arr(&r->arena);if(!container.p)return 0;rt_set(r,(GscObj*)object.p,field,container);}
    if(container.kind!=V_ARRAY)return 0;
    return arr_set_string((GscArr*)container.p,prog_intern(&r->prog,key),v) && errors==r->errors;
}
int gsc_runtime_notify(GscRuntime* r,int entity,const char* event,const GscArgument* args,int argc) {
    GscVal values[GSC_ARGUMENT_MAX];int i,name;if(!r || !event || argc<0 || argc>GSC_ARGUMENT_MAX || (argc && !args))return 0;
    name=prog_intern(&r->prog,event);if(name<0)return 0;
    for(i=0;i<argc;i++)values[i]=rt_host_argument(r,&args[i]);
    return rt_notify_values(r,rt_entity(r,entity),name,values,argc,NULL);
}
int gsc_runtime_threads(const GscRuntime* r) {
    int i,n=0;if(!r)return 0;for(i=0;i<RT_THREADS;i++)if(r->threads[i] && r->threads[i]->state!=3)n++;return n;
}
int gsc_runtime_errors(const GscRuntime* r){return r?r->errors:0;}
const char* gsc_runtime_level_function(const GscRuntime* r,const char* field) {
    int n;GscVal v;if(!r || !field)return "";
    n=obj_find((GscObj*)r->level.p,field);if(n<0)return "";
    v=((GscObj*)r->level.p)->fields[n].val;
    return v.kind==V_FUNCTION && v.i>=0 && v.i<r->prog.nfunc?r->prog.funcs[v.i].name:"";
}
int gsc_runtime_start_dispatched(const GscRuntime* r){return r?r->start_dispatched:0;}
int gsc_runtime_start_complete(const GscRuntime* r){return r?r->start_complete:0;}
int gsc_runtime_loading_pending(const GscRuntime* r){
    int i;if(!r)return 0;if(r->start_requested && !r->start_complete)return 1;
    for(i=0;i<RT_THREADS;i++){const RtThread* t=r->threads[i];if(t && (t->state==0 || (t->state==1 && t->wake<=r->now)))return 1;}
    return 0;
}
int gsc_runtime_level_int(const GscRuntime* r,const char* field,int* value) {
    int n;if(!r || !field || !value)return 0;n=obj_find((GscObj*)r->level.p,field);
    if(n<0 || ((GscObj*)r->level.p)->fields[n].val.kind!=V_INT)return 0;
    *value=((GscObj*)r->level.p)->fields[n].val.i;return 1;
}
const char* gsc_runtime_game_string(const GscRuntime* r,const char* field){
    int n;if(!r || !field)return "";n=obj_find((GscObj*)r->game.p,field);
    if(n<0 || ((GscObj*)r->game.p)->fields[n].val.kind!=V_STR)return "";
    return str_at(&r->prog,((GscObj*)r->game.p)->fields[n].val.s);
}

int gsc_runtime_is_waiting(const GscRuntime* r,int entity,const char* event){
    int i,id;if(!r || !event)return 0;
    for(i=0;i<RT_THREADS;i++){const RtThread* t=r->threads[i];if(t && t->state==2 && rt_id(t->event_receiver,&id) && id==entity && name_equal(str_at(&r->prog,t->event_name),event))return 1;}
    return 0;
}

void gsc_runtime_precache_dependencies(GscRuntime* r){
    int i;if(!r || r->running || !r->host.precache || !r->host.sound_alias_exists)return;
    for(i=0;i<r->prog.ncode;i++){
        const GscIns in=r->prog.code[i];
        if(in.op==GSC_PUSH_STR){const char* name=str_at(&r->prog,in.str);if(r->host.sound_alias_exists(r->host.user,name))r->host.precache(r->host.user,GSC_PC_SOUND,name);}
        if(i && in.op==GSC_METHOD && in.b==1 && r->prog.code[i-1].op==GSC_PUSH_STR){
            const char* method=str_at(&r->prog,in.a);
            if(name_equal(method,"playSound") || name_equal(method,"playLoopSound") || name_equal(method,"playLocalSound"))r->host.precache(r->host.user,GSC_PC_SOUND,str_at(&r->prog,r->prog.code[i-1].str));
        }
    }
}
