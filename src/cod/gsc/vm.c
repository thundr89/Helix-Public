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
 * that only want the weapon list. thread runs the call once, inline.
 * GSC_STOP ends that branch and the caller continues.
 */
#include "gsc/vm.h"

#include "gsc/compile.h"
#include "gsc/op.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gsc_vm_last_int;

#define GSC_STACK_MAX 1024
#define GSC_FRAME_MAX 64
#define GSC_CALL_DEPTH_CAP 32

typedef enum GscVk {
    V_UNDEF = 0,
    V_INT,
    V_FLOAT,
    V_STR,
    V_ARRAY,
    V_OBJECT
} GscVk;

typedef struct GscVal {
    GscVk kind;
    int i;
    double f;
    int s; /* string table index */
    void* p; /* GscArr* when V_ARRAY, GscObj* when V_OBJECT */
} GscVal;

typedef struct GscFrame {
    int return_ip;  /* ip to resume at in the caller; -1 for the entry frame */
    int nlocals;
    GscVal* locals; /* heap-allocated; freed on return */
    int from_thread; /* 1 if GSC_THREAD entered this frame */
    int sp_base;     /* caller stack depth after this frame's args were popped */
} GscFrame;

typedef struct GscArr {
    GscVal* items;
    int size;
    int cap;
} GscArr;

typedef struct GscField {
    char key[64];
    GscVal val;
} GscField;

typedef struct GscObj {
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
} GscArena;

static GscVal make_undef(void) {
    GscVal v;
    v.kind = V_UNDEF;
    v.i = 0;
    v.f = 0.0;
    v.s = 0;
    v.p = NULL;
    return v;
}

static GscVal make_int(int x) {
    GscVal v;
    v.kind = V_INT;
    v.i = x;
    v.f = 0.0;
    v.s = 0;
    v.p = NULL;
    return v;
}

static GscVal make_float(double x) {
    GscVal v;
    v.kind = V_FLOAT;
    v.i = 0;
    v.f = x;
    v.s = 0;
    v.p = NULL;
    return v;
}

static int arena_record(GscArena* ar, void* obj, int kind) {
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
    if (!arena_record(ar, a, 0)) {
        free(a);
        return NULL;
    }
    return a;
}

static GscObj* arena_new_obj(GscArena* ar) {
    GscObj* o = (GscObj*)calloc(1, sizeof(GscObj));
    if (!o) return NULL;
    if (!arena_record(ar, o, 1)) {
        free(o);
        return NULL;
    }
    return o;
}

static void arena_free(GscArena* ar) {
    int i;
    for (i = 0; i < ar->n; i++) {
        if (ar->kinds[i] == 0) {
            GscArr* a = (GscArr*)ar->items[i];
            if (a) {
                free(a->items);
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
    a->items[idx] = v;
}

static int obj_find(const GscObj* o, const char* key) {
    int i;
    if (!o || !key) return -1;
    for (i = 0; i < o->size; i++) {
        if (strcmp(o->fields[i].key, key) == 0) return i;
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
static int prog_intern(GscProg* p, const char* s) {
    size_t len;
    int i;
    int end = 0;
    char* ns;
    if (!p || !s) return -1;
    len = strlen(s);
    for (i = 0; i < p->nstr; i++) {
        const char* cur = p->strs + p->stroff[i];
        int e = p->stroff[i] + (int)strlen(cur) + 1;
        if (strcmp(cur, s) == 0) return i;
        if (e > end) end = e;
    }
    ns = (char*)realloc(p->strs, (size_t)end + len + 1);
    if (!ns) return -1;
    p->strs = ns;
    if (p->nstr >= p->scap) {
        int nc = p->scap ? p->scap * 2 : 8;
        int* no = (int*)realloc(p->stroff, (size_t)nc * sizeof(int));
        if (!no) return -1;
        p->stroff = no;
        p->scap = nc;
    }
    memcpy(p->strs + end, s, len + 1);
    p->stroff[p->nstr] = end;
    return p->nstr++;
}

static int val_truthy(const GscVal* v) {
    if (v->kind == V_INT) return v->i != 0;
    if (v->kind == V_FLOAT) return v->f != 0.0;
    if (v->kind == V_STR) return 1;
    return 0;
}

static int val_eq(const GscVal* a, const GscVal* b) {
    if (a->kind == V_INT && b->kind == V_INT) return a->i == b->i;
    if ((a->kind == V_INT || a->kind == V_FLOAT) &&
        (b->kind == V_INT || b->kind == V_FLOAT)) {
        double da = a->kind == V_INT ? (double)a->i : a->f;
        double db = b->kind == V_INT ? (double)b->i : b->f;
        return da == db;
    }
    if (a->kind == V_STR && b->kind == V_STR) return a->s == b->s;
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

static GscVal val_add(const GscVal* a, const GscVal* b) {
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

static GscVal val_sub(const GscVal* a, const GscVal* b) {
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
    if (v->kind == V_INT || v->kind == V_FLOAT || v->kind == V_UNDEF)
        return *v;
    if (v->kind == V_STR) {
        int ni = prog_intern(dst, str_at(src, v->s));
        out = make_undef();
        if (ni < 0) return out;
        out.kind = V_STR;
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
            for (i = 0; i < sa->size; i++)
                na->items[i] = copy_val_rec(&sa->items[i], src, dst, arena,
                                            seen, nseen, scap);
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
    if (v.kind != V_STR && v.kind != V_ARRAY && v.kind != V_OBJECT) return v;
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
        case GSC_PUSH_FLOAT:
            if (sp < GSC_STACK_MAX) stack[sp++] = make_float(ins->f);
            break;
        case GSC_PUSH_STR: {
            GscVal v = make_undef();
            v.kind = V_STR;
            v.s = ins->str;
            if (sp < GSC_STACK_MAX) stack[sp++] = v;
            break;
        }
        case GSC_GET_LOCAL: {
            GscFrame* f = &frames[fp - 1];
            if (ins->a >= 0 && ins->a < f->nlocals) {
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
            if (sp < GSC_STACK_MAX) stack[sp++] = val_add(&a, &b);
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
            if (base.kind == V_ARRAY && base.p && idx.kind == V_INT) {
                GscArr* a = (GscArr*)base.p;
                if (idx.i >= 0 && idx.i < a->size) out = a->items[idx.i];
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
                out = make_int(a ? a->size : 0);
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
            sp -= argc;
            if (sp < 0) sp = 0;
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
