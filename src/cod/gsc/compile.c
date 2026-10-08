/*
 * GSC source-to-bytecode compiler. The output program is executed by vm.c.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The grammar is small on purpose. Task 1 covers return, if / else, local
 * assignments, and expression calls. Expressions are integers, string
 * literals, identifiers, + - * /, parentheses, <, == and name(expr, ...)
 * calls. A path::name(...) call emits GSC_FAR. thread name() and
 * thread path::name() emit GSC_THREAD. wait, waittill, getent, and a
 * call on self emit GSC_STOP.
 */
#include "gsc/compile.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSC_MAX_FUNCS 64
#define GSC_MAX_LOCALS 64
#define GSC_MAX_LOOPS 16
#define GSC_MAX_LABEL_PATCHES 64
#define GSC_MAX_CASES 128
#define GSC_SCRATCH_NAME "@scratch"

typedef struct FuncDesc {
    char name[64];
    const char* params;
    size_t params_len;
    const char* body;
    size_t body_len;
} FuncDesc;

typedef struct LoopCtx {
    int kind; /* 0 = loop, 1 = switch */
    int breaks[GSC_MAX_LABEL_PATCHES];
    int nbreaks;
    int continues[GSC_MAX_LABEL_PATCHES];
    int ncontinues;
} LoopCtx;

typedef struct Comp {
    GscProg* prog;
    int strs_cap;
    int strs_len;
    char locals[GSC_MAX_LOCALS][64];
    int nlocals;
    int scratch;
    LoopCtx loops[GSC_MAX_LOOPS];
    int nloops;
} Comp;

typedef struct Cur {
    const char* p;
    const char* end;
} Cur;

/* -------- lexing helpers -------- */

static void skip_ws(Cur* c) {
    while (c->p < c->end && isspace((unsigned char)*c->p)) c->p++;
}

static int read_ident(Cur* c, char* out, size_t cap) {
    size_t n = 0;
    skip_ws(c);
    if (c->p >= c->end) return 0;
    if (!(isalpha((unsigned char)*c->p) || *c->p == '_')) return 0;
    while (c->p < c->end && (isalnum((unsigned char)*c->p) || *c->p == '_')) {
        if (n + 1 < cap) out[n++] = *c->p;
        c->p++;
    }
    out[n] = '\0';
    return 1;
}

static const char* match_brace(const char* p, const char* end) {
    int d = 0;
    if (p >= end || *p != '{') return NULL;
    for (; p < end; p++) {
        if (*p == '{') d++;
        else if (*p == '}') {
            d--;
            if (d == 0) return p + 1;
        }
    }
    return NULL;
}

/* -------- program growth -------- */

static int emit_ins(GscProg* p, int op, int a, int b, int str, double f) {
    if (p->ncode >= p->cap) {
        int nc = p->cap ? p->cap * 2 : 128;
        GscIns* nn = (GscIns*)realloc(p->code, (size_t)nc * sizeof(GscIns));
        if (!nn) return -1;
        p->code = nn;
        p->cap = nc;
    }
    p->code[p->ncode].op = (unsigned char)op;
    p->code[p->ncode].a = a;
    p->code[p->ncode].b = b;
    p->code[p->ncode].str = str;
    p->code[p->ncode].f = f;
    return p->ncode++;
}

static int intern_str(Comp* co, const char* s, size_t len) {
    GscProg* p = co->prog;
    int i;
    for (i = 0; i < p->nstr; i++) {
        const char* cur = p->strs + p->stroff[i];
        if (strlen(cur) == len && memcmp(cur, s, len) == 0) return i;
    }
    if (co->strs_len + (int)len + 1 > co->strs_cap) {
        int nc = co->strs_cap ? co->strs_cap * 2 : 128;
        char* ns;
        while (nc < co->strs_len + (int)len + 1) nc *= 2;
        ns = (char*)realloc(p->strs, (size_t)nc);
        if (!ns) return -1;
        p->strs = ns;
        co->strs_cap = nc;
    }
    if (p->nstr >= p->scap) {
        int nc = p->scap ? p->scap * 2 : 32;
        int* no = (int*)realloc(p->stroff, (size_t)nc * sizeof(int));
        if (!no) return -1;
        p->stroff = no;
        p->scap = nc;
    }
    p->stroff[p->nstr] = co->strs_len;
    memcpy(p->strs + co->strs_len, s, len);
    p->strs[co->strs_len + (int)len] = '\0';
    co->strs_len += (int)len + 1;
    return p->nstr++;
}

/* -------- local slots -------- */

static int local_slot(Comp* co, const char* name) {
    int i;
    for (i = 0; i < co->nlocals; i++) {
        if (strcmp(co->locals[i], name) == 0) return i;
    }
    return -1;
}

static int local_add(Comp* co, const char* name) {
    int i = local_slot(co, name);
    if (i >= 0) return i;
    if (co->nlocals >= GSC_MAX_LOCALS) return co->nlocals - 1;
    snprintf(co->locals[co->nlocals], 64, "%s", name);
    return co->nlocals++;
}

static int scratch_slot(Comp* co) {
    if (co->scratch < 0) co->scratch = local_add(co, GSC_SCRATCH_NAME);
    return co->scratch;
}

/* -------- function and builtin lookup -------- */

static int find_func_index(GscProg* p, const char* name) {
    int i;
    for (i = 0; i < p->nfunc; i++) {
        if (strcmp(p->funcs[i].name, name) == 0) return i;
    }
    return -1;
}

static int builtin_id(const char* name) {
    if (strcmp(name, "giveWeapon") == 0 || strcmp(name, "giveweapon") == 0) return GSC_BU_GIVE;
    if (strcmp(name, "takeWeapon") == 0 || strcmp(name, "takeAllWeapons") == 0) return GSC_BU_TAKE;
    if (strcmp(name, "ambientPlay") == 0) return GSC_BU_AMBIENT;
    if (strcmp(name, "spawnstruct") == 0) return GSC_BU_STRUCT;
    return 0;
}

/* Builtins that push a value on the stack (so a statement-level call to
 * them must discard it). Most host-side builtins push nothing. */
static int builtin_pushes(int id) {
    return id == GSC_BU_STRUCT;
}

/* -------- expressions -------- */

static int compile_expr(Comp* co, Cur* c);

static int compile_postfix(Comp* co, Cur* c) {
    for (;;) {
        skip_ws(c);
        if (c->p >= c->end) break;
        if (*c->p == '.') {
            char field[64];
            int si;
            c->p++;
            if (!read_ident(c, field, sizeof(field))) return -1;
            si = intern_str(co, field, strlen(field));
            emit_ins(co->prog, GSC_GET_FIELD, si, 0, si, 0.0);
            continue;
        }
        if (*c->p == '[') {
            c->p++;
            if (compile_expr(co, c) < 0) return -1;
            skip_ws(c);
            if (c->p < c->end && *c->p == ']') c->p++;
            emit_ins(co->prog, GSC_GET_INDEX, 0, 0, -1, 0.0);
            continue;
        }
        break;
    }
    return 0;
}

/* Backslash and :: always start a far call. A slash does so only when a
 * later :: appears before an operator, so `a / 2` and `a /= b` stay division. */
static int starts_far_call(const Cur* c) {
    const char* p = c->p;
    const char* end = c->end;
    if (p >= end) return 0;
    if (*p == '\\') return 1;
    if (p + 1 < end && p[0] == ':' && p[1] == ':') return 1;
    if (*p != '/') return 0;
    for (;;) {
        if (p >= end || (*p != '/' && *p != '\\')) return 0;
        p++;
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end || !(isalpha((unsigned char)*p) || *p == '_')) return 0;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p + 1 < end && p[0] == ':' && p[1] == ':') return 1;
        if (p < end && (*p == '/' || *p == '\\')) continue;
        return 0;
    }
}

static int path_append(char* path, size_t cap, const char* piece) {
    size_t n = strlen(path);
    size_t m = strlen(piece);
    if (n + m >= cap) return -1;
    memcpy(path + n, piece, m + 1);
    return 0;
}

/* path::name(args). Backslashes become slashes. A missing .gsc suffix is
 * added. a = path string index, b = function name string index, str = argc. */
static int compile_far_call(Comp* co, Cur* c, const char* first) {
    char path[512];
    char fname[64];
    char seg[64];
    int argc = 0;
    int pi, ni;
    size_t len;

    path[0] = '\0';
    if (path_append(path, sizeof(path), first) < 0) return -1;
    for (;;) {
        skip_ws(c);
        if (c->p >= c->end || (*c->p != '\\' && *c->p != '/')) break;
        c->p++;
        if (!read_ident(c, seg, sizeof(seg))) return -1;
        if (path_append(path, sizeof(path), "/") < 0) return -1;
        if (path_append(path, sizeof(path), seg) < 0) return -1;
    }
    skip_ws(c);
    if (!(c->p + 1 < c->end && c->p[0] == ':' && c->p[1] == ':')) return -1;
    c->p += 2;
    if (!read_ident(c, fname, sizeof(fname))) return -1;
    skip_ws(c);
    if (!(c->p < c->end && *c->p == '(')) return -1;
    c->p++;
    skip_ws(c);
    if (!(c->p < c->end && *c->p == ')')) {
        for (;;) {
            if (compile_expr(co, c) < 0) return -1;
            argc++;
            skip_ws(c);
            if (c->p < c->end && *c->p == ',') { c->p++; continue; }
            break;
        }
    }
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;
    len = strlen(path);
    if (!(len >= 4 && strcmp(path + len - 4, ".gsc") == 0)) {
        if (path_append(path, sizeof(path), ".gsc") < 0) return -1;
    }
    pi = intern_str(co, path, strlen(path));
    ni = intern_str(co, fname, strlen(fname));
    if (pi < 0 || ni < 0) return -1;
    emit_ins(co->prog, GSC_FAR, pi, ni, argc, 0.0);
    return compile_postfix(co, c);
}

static int compile_prim(Comp* co, Cur* c) {
    skip_ws(c);
    if (c->p >= c->end) return -1;
    if (*c->p == '-') {
        /* Unary minus. Compile as 0 - prim so the int-int path uses the
         * wrapping GSC_SUB and -2147483648 ends up with the INT_MIN bit
         * pattern on the stack. */
        c->p++;
        emit_ins(co->prog, GSC_PUSH_INT, 0, 0, -1, 0.0);
        if (compile_prim(co, c) < 0) return -1;
        emit_ins(co->prog, GSC_SUB, 0, 0, -1, 0.0);
        return 0;
    }
    if (*c->p == '[') {
        /* Empty-array literal `[]`. */
        c->p++;
        skip_ws(c);
        if (c->p < c->end && *c->p == ']') c->p++;
        emit_ins(co->prog, GSC_NEW_ARRAY, 0, 0, -1, 0.0);
        return 0;
    }
    if (*c->p == '(') {
        c->p++;
        if (compile_expr(co, c) < 0) return -1;
        skip_ws(c);
        if (c->p < c->end && *c->p == ')') c->p++;
        return 0;
    }
    if (*c->p == '"') {
        const char* s;
        int si;
        c->p++;
        s = c->p;
        while (c->p < c->end && *c->p != '"') c->p++;
        si = intern_str(co, s, (size_t)(c->p - s));
        if (c->p < c->end && *c->p == '"') c->p++;
        emit_ins(co->prog, GSC_PUSH_STR, 0, 0, si, 0.0);
        return 0;
    }
    /* One numeric token. A fraction (.32 or 0.32) is a float. An integer
     * with no fraction keeps the uint32_t wrap, so 2147483648 is INT_MIN. */
    if (isdigit((unsigned char)*c->p) ||
        (*c->p == '.' && c->p + 1 < c->end && isdigit((unsigned char)c->p[1]))) {
        char buf[64];
        size_t n = 0;
        int is_float = 0;
        if (*c->p == '.') {
            is_float = 1;
            buf[n++] = *c->p++;
            while (c->p < c->end && isdigit((unsigned char)*c->p) && n + 1 < sizeof(buf))
                buf[n++] = *c->p++;
        } else {
            while (c->p < c->end && isdigit((unsigned char)*c->p) && n + 1 < sizeof(buf))
                buf[n++] = *c->p++;
            if (c->p < c->end && *c->p == '.' &&
                c->p + 1 < c->end && isdigit((unsigned char)c->p[1])) {
                is_float = 1;
                if (n + 1 < sizeof(buf)) buf[n++] = *c->p++;
                while (c->p < c->end && isdigit((unsigned char)*c->p) && n + 1 < sizeof(buf))
                    buf[n++] = *c->p++;
            }
        }
        buf[n] = '\0';
        if (is_float) {
            emit_ins(co->prog, GSC_PUSH_FLOAT, 0, 0, -1, strtod(buf, NULL));
        } else {
            unsigned long long v;
            int32_t sv;
            v = strtoull(buf, NULL, 10);
            sv = (int32_t)(uint32_t)v;
            emit_ins(co->prog, GSC_PUSH_INT, (int)sv, 0, -1, 0.0);
        }
        return 0;
    }
    if (isalpha((unsigned char)*c->p) || *c->p == '_') {
        char name[64];
        if (!read_ident(c, name, sizeof(name))) return -1;
        skip_ws(c);
        if (starts_far_call(c)) return compile_far_call(co, c, name);
        if (c->p < c->end && *c->p == '(') {
            int argc = 0;
            int fi;
            int bi;
            if (strcmp(name, "getent") == 0) {
                /* No entity lookup. The call is the end of this branch. */
                int depth = 0;
                for (; c->p < c->end; c->p++) {
                    if (*c->p == '(') depth++;
                    else if (*c->p == ')') {
                        depth--;
                        if (depth == 0) { c->p++; break; }
                    }
                }
                emit_ins(co->prog, GSC_STOP, 0, 0, -1, 0.0);
                return 0;
            }
            c->p++;
            skip_ws(c);
            if (!(c->p < c->end && *c->p == ')')) {
                for (;;) {
                    if (compile_expr(co, c) < 0) return -1;
                    argc++;
                    skip_ws(c);
                    if (c->p < c->end && *c->p == ',') { c->p++; continue; }
                    break;
                }
            }
            skip_ws(c);
            if (c->p < c->end && *c->p == ')') c->p++;
            fi = find_func_index(co->prog, name);
            if (fi >= 0) {
                emit_ins(co->prog, GSC_CALL, fi, argc, -1, 0.0);
                return compile_postfix(co, c);
            }
            bi = builtin_id(name);
            if (bi) {
                emit_ins(co->prog, GSC_BUILTIN, bi, argc, -1, 0.0);
                /* Only STRUCT (spawnstruct) pushes a value, so postfix
                 * only makes sense there. The rest are host callbacks. */
                if (builtin_pushes(bi)) return compile_postfix(co, c);
                return 0;
            }
            /* Unknown call: pop its args, push undefined. We model this with a
             * builtin id 0 that the runtime treats as a no-op pop. */
            emit_ins(co->prog, GSC_BUILTIN, 0, argc, -1, 0.0);
            emit_ins(co->prog, GSC_PUSH_UNDEF, 0, 0, -1, 0.0);
            return compile_postfix(co, c);
        }
        {
            int slot = local_slot(co, name);
            if (slot < 0) emit_ins(co->prog, GSC_PUSH_UNDEF, 0, 0, -1, 0.0);
            else emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        }
        return compile_postfix(co, c);
    }
    return -1;
}

static int compile_mul(Comp* co, Cur* c) {
    if (compile_prim(co, c) < 0) return -1;
    for (;;) {
        char op;
        skip_ws(c);
        if (c->p >= c->end) break;
        if (*c->p != '*' && *c->p != '/') break;
        op = *c->p;
        c->p++;
        if (compile_prim(co, c) < 0) return -1;
        emit_ins(co->prog, op == '*' ? GSC_MUL : GSC_DIV, 0, 0, -1, 0.0);
    }
    return 0;
}

static int compile_add(Comp* co, Cur* c) {
    if (compile_mul(co, c) < 0) return -1;
    for (;;) {
        char op;
        skip_ws(c);
        if (c->p >= c->end) break;
        if (*c->p != '+' && *c->p != '-') break;
        /* Avoid consuming ++, --, +=, -=. */
        if (c->p + 1 < c->end && (c->p[1] == '=' || c->p[1] == *c->p)) break;
        op = *c->p;
        c->p++;
        if (compile_mul(co, c) < 0) return -1;
        emit_ins(co->prog, op == '+' ? GSC_ADD : GSC_SUB, 0, 0, -1, 0.0);
    }
    return 0;
}

static int compile_cmp(Comp* co, Cur* c) {
    if (compile_add(co, c) < 0) return -1;
    for (;;) {
        skip_ws(c);
        if (c->p >= c->end) break;
        if (*c->p == '<' && (c->p + 1 >= c->end || (c->p[1] != '<' && c->p[1] != '='))) {
            c->p++;
            if (compile_add(co, c) < 0) return -1;
            emit_ins(co->prog, GSC_LT, 0, 0, -1, 0.0);
            continue;
        }
        if (*c->p == '>' && (c->p + 1 >= c->end || (c->p[1] != '>' && c->p[1] != '='))) {
            c->p++;
            if (compile_add(co, c) < 0) return -1;
            emit_ins(co->prog, GSC_GT, 0, 0, -1, 0.0);
            continue;
        }
        if (c->p + 1 < c->end && c->p[0] == '=' && c->p[1] == '=') {
            c->p += 2;
            if (compile_add(co, c) < 0) return -1;
            emit_ins(co->prog, GSC_EQ, 0, 0, -1, 0.0);
            continue;
        }
        break;
    }
    return 0;
}

static int compile_expr(Comp* co, Cur* c) {
    return compile_cmp(co, c);
}

/* -------- statements -------- */

static int compile_stmt(Comp* co, Cur* c);

/* Drop the rest of a statement that failed. Stop at a semicolon at brace
 * depth 0, or after one {...} group. A } at depth 0 closes the function
 * and is left for the caller. */
static void skip_failed_stmt(Cur* c) {
    int depth = 0;
    while (c->p < c->end) {
        char ch = *c->p;
        if (ch == '"') {
            c->p++;
            while (c->p < c->end && *c->p != '"') c->p++;
            if (c->p < c->end) c->p++;
            continue;
        }
        if (ch == '{') {
            depth++;
            c->p++;
            continue;
        }
        if (ch == '}') {
            if (depth == 0) return;
            depth--;
            c->p++;
            if (depth == 0) return;
            continue;
        }
        if (ch == ';' && depth == 0) {
            c->p++;
            return;
        }
        c->p++;
    }
}

/* A failed expression used to end the whole function. Roll back to the
 * instructions from before this statement so a half-emitted call does not
 * leave stray pushes, skip the rest of the statement, and keep going. */
static void compile_stmt_keep(Comp* co, Cur* c) {
    int snap = co->prog->ncode;
    const char* at = c->p;
    if (compile_stmt(co, c) < 0) {
        co->prog->ncode = snap;
        skip_failed_stmt(c);
        if (c->p == at && c->p < c->end && *c->p != '}') c->p++;
    }
}

static int compile_block(Comp* co, Cur* c) {
    skip_ws(c);
    if (c->p < c->end && *c->p == '{') {
        c->p++;
        for (;;) {
            skip_ws(c);
            if (c->p >= c->end) break;
            if (*c->p == '}') { c->p++; break; }
            compile_stmt_keep(co, c);
        }
        return 0;
    }
    return compile_stmt(co, c);
}

static void skip_to_semi(Cur* c) {
    while (c->p < c->end && *c->p != ';' && *c->p != '}') c->p++;
    if (c->p < c->end && *c->p == ';') c->p++;
}

/* `level.<name> = <expr> ;`. The handled fields emit a BUILTIN that takes the
 * expression value and does not push. Any other field is accepted but
 * ignored so the caller still terminates the statement. */
static int compile_level_assign(Comp* co, Cur* c) {
    char field[64];
    int handled;
    skip_ws(c);
    if (c->p >= c->end || *c->p != '.') { skip_to_semi(c); return 0; }
    c->p++;
    if (!read_ident(c, field, sizeof(field))) { skip_to_semi(c); return 0; }
    skip_ws(c);
    if (c->p >= c->end || *c->p != '=' ||
        (c->p + 1 < c->end && c->p[1] == '=')) {
        skip_to_semi(c);
        return 0;
    }
    c->p++;
    if (compile_expr(co, c) < 0) return -1;
    skip_ws(c);
    if (c->p < c->end && *c->p == ';') c->p++;
    handled = (strcmp(field, "gametype") == 0 || strcmp(field, "gamemode") == 0);
    if (handled) {
        emit_ins(co->prog, GSC_BUILTIN, GSC_BU_GAMETYPE, 1, -1, 0.0);
    } else {
        /* The expression pushed one value we no longer need. */
        emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
    }
    return 0;
}

/* `game["allies"] = <expr> ;` and `game["axis"] = <expr> ;`. */
static int compile_game_assign(Comp* co, Cur* c) {
    char key[64];
    size_t k = 0;
    int handled = 0;
    key[0] = '\0';
    skip_ws(c);
    if (c->p >= c->end || *c->p != '[') { skip_to_semi(c); return 0; }
    c->p++;
    skip_ws(c);
    if (c->p < c->end && *c->p == '"') {
        c->p++;
        while (c->p < c->end && *c->p != '"' && k + 1 < sizeof(key)) key[k++] = *c->p++;
        key[k] = '\0';
        if (c->p < c->end && *c->p == '"') c->p++;
    }
    skip_ws(c);
    if (c->p < c->end && *c->p == ']') c->p++;
    skip_ws(c);
    if (c->p >= c->end || *c->p != '=' ||
        (c->p + 1 < c->end && c->p[1] == '=')) {
        skip_to_semi(c);
        return 0;
    }
    c->p++;
    if (compile_expr(co, c) < 0) return -1;
    skip_ws(c);
    if (c->p < c->end && *c->p == ';') c->p++;
    if (strcmp(key, "allies") == 0) {
        emit_ins(co->prog, GSC_BUILTIN, GSC_BU_ALLIES, 1, -1, 0.0);
        handled = 1;
    } else if (strcmp(key, "axis") == 0) {
        emit_ins(co->prog, GSC_BUILTIN, GSC_BU_AXIS, 1, -1, 0.0);
        handled = 1;
    }
    if (!handled) emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
    return 0;
}

/* Common LHS compiler for assignment-style statements and for-step clauses.
 * `name` is the identifier that was already read, `save` points to just
 * before it in `c` so the caller can rewind for a function-call parse.
 * `need_semi` consumes a trailing `;`. Supports:
 *   name = expr        name.field = expr      name[expr] = expr
 *   name += expr etc.  name++ / name--        funcname(args) / builtin
 */
static int compile_lhs(Comp* co, Cur* c, const char* name,
                       const char* save, int need_semi) {
    skip_ws(c);
    if (c->p >= c->end) {
        if (need_semi) skip_to_semi(c);
        return 0;
    }
    if (*c->p == '(' || starts_far_call(c)) {
        /* Function, builtin, or path::name call. Rewind and let compile_prim parse it. */
        int before = co->prog->ncode;
        int last;
        c->p = save;
        if (compile_prim(co, c) < 0) return -1;
        last = co->prog->ncode - 1;
        if (last >= before) {
            int discard = 1;
            if (co->prog->code[last].op == GSC_BUILTIN) {
                int bid = co->prog->code[last].a;
                if (!builtin_pushes(bid)) discard = 0;
            }
            if (discard) emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
        }
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (c->p + 1 < c->end && c->p[0] == '+' && c->p[1] == '+') {
        int slot = local_add(co, name);
        c->p += 2;
        emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        emit_ins(co->prog, GSC_PUSH_INT, 1, 0, -1, 0.0);
        emit_ins(co->prog, GSC_ADD, 0, 0, -1, 0.0);
        emit_ins(co->prog, GSC_SET_LOCAL, slot, 0, -1, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (c->p + 1 < c->end && c->p[0] == '-' && c->p[1] == '-') {
        int slot = local_add(co, name);
        c->p += 2;
        emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        emit_ins(co->prog, GSC_PUSH_INT, 1, 0, -1, 0.0);
        emit_ins(co->prog, GSC_SUB, 0, 0, -1, 0.0);
        emit_ins(co->prog, GSC_SET_LOCAL, slot, 0, -1, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (c->p + 1 < c->end &&
        (c->p[0] == '+' || c->p[0] == '-' || c->p[0] == '*' || c->p[0] == '/') &&
        c->p[1] == '=') {
        char opc = c->p[0];
        int slot;
        int op;
        c->p += 2;
        slot = local_add(co, name);
        emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        if (compile_expr(co, c) < 0) return -1;
        op = opc == '+' ? GSC_ADD :
             opc == '-' ? GSC_SUB :
             opc == '*' ? GSC_MUL : GSC_DIV;
        emit_ins(co->prog, op, 0, 0, -1, 0.0);
        emit_ins(co->prog, GSC_SET_LOCAL, slot, 0, -1, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (*c->p == '=' && (c->p + 1 >= c->end || c->p[1] != '=')) {
        int slot;
        c->p++;
        if (compile_expr(co, c) < 0) return -1;
        slot = local_add(co, name);
        emit_ins(co->prog, GSC_SET_LOCAL, slot, 0, -1, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (*c->p == '.') {
        /* name.field = expr */
        char field[64];
        int slot;
        int si;
        c->p++;
        if (!read_ident(c, field, sizeof(field))) {
            if (need_semi) skip_to_semi(c);
            return 0;
        }
        skip_ws(c);
        if (c->p >= c->end || *c->p != '=' ||
            (c->p + 1 < c->end && c->p[1] == '=')) {
            if (need_semi) skip_to_semi(c);
            return 0;
        }
        c->p++;
        slot = local_add(co, name);
        emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        if (compile_expr(co, c) < 0) return -1;
        si = intern_str(co, field, strlen(field));
        emit_ins(co->prog, GSC_SET_FIELD, si, 0, si, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (*c->p == '[') {
        /* name[expr] = expr */
        int slot;
        c->p++;
        slot = local_add(co, name);
        emit_ins(co->prog, GSC_GET_LOCAL, slot, 0, -1, 0.0);
        if (compile_expr(co, c) < 0) return -1;
        skip_ws(c);
        if (c->p < c->end && *c->p == ']') c->p++;
        skip_ws(c);
        if (c->p >= c->end || *c->p != '=' ||
            (c->p + 1 < c->end && c->p[1] == '=')) {
            /* Not an assignment: pop the two values we pushed. */
            emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
            emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
            if (need_semi) skip_to_semi(c);
            return 0;
        }
        c->p++;
        if (compile_expr(co, c) < 0) return -1;
        emit_ins(co->prog, GSC_SET_INDEX, 0, 0, -1, 0.0);
        if (need_semi) {
            skip_ws(c);
            if (c->p < c->end && *c->p == ';') c->p++;
        }
        return 0;
    }
    if (need_semi) skip_to_semi(c);
    return 0;
}

static int compile_body_stmt(Comp* co, Cur* c);

static int compile_while(Comp* co, Cur* c) {
    int cond_ip;
    int jif;
    LoopCtx* ctx = NULL;
    int k;
    skip_ws(c);
    if (c->p < c->end && *c->p == '(') c->p++;
    cond_ip = co->prog->ncode;
    if (compile_expr(co, c) < 0) return -1;
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;
    jif = emit_ins(co->prog, GSC_JUMP_IF_FALSE, 0, 0, -1, 0.0);
    if (co->nloops < GSC_MAX_LOOPS) {
        ctx = &co->loops[co->nloops++];
        ctx->kind = 0;
        ctx->nbreaks = 0;
        ctx->ncontinues = 0;
    }
    if (compile_body_stmt(co, c) < 0) {
        co->prog->code[jif].a = co->prog->ncode;
        if (ctx) {
            for (k = 0; k < ctx->nbreaks; k++)
                co->prog->code[ctx->breaks[k]].a = co->prog->ncode;
            for (k = 0; k < ctx->ncontinues; k++)
                co->prog->code[ctx->continues[k]].a = co->prog->ncode;
            co->nloops--;
        }
        return -1;
    }
    if (ctx) {
        for (k = 0; k < ctx->ncontinues; k++)
            co->prog->code[ctx->continues[k]].a = cond_ip;
    }
    emit_ins(co->prog, GSC_JUMP_BACK, cond_ip, 0, -1, 0.0);
    co->prog->code[jif].a = co->prog->ncode;
    if (ctx) {
        for (k = 0; k < ctx->nbreaks; k++)
            co->prog->code[ctx->breaks[k]].a = co->prog->ncode;
        co->nloops--;
    }
    return 0;
}

static int compile_for(Comp* co, Cur* c) {
    int cond_ip;
    int jif;
    int body_jump;
    int step_ip;
    LoopCtx* ctx = NULL;
    int k;
    char iname[64];
    const char* save;

    skip_ws(c);
    if (c->p < c->end && *c->p == '(') c->p++;
    /* init: usually `i = 0;` */
    skip_ws(c);
    save = c->p;
    if (c->p < c->end && *c->p != ';') {
        if (!read_ident(c, iname, sizeof(iname))) {
            skip_to_semi(c);
        } else if (compile_lhs(co, c, iname, save, 1) < 0) {
            return -1;
        }
    } else {
        c->p++; /* bare `;` */
    }
    /* cond */
    cond_ip = co->prog->ncode;
    skip_ws(c);
    if (c->p < c->end && *c->p != ';') {
        if (compile_expr(co, c) < 0) return -1;
    } else {
        emit_ins(co->prog, GSC_PUSH_INT, 1, 0, -1, 0.0);
    }
    skip_ws(c);
    if (c->p < c->end && *c->p == ';') c->p++;
    jif = emit_ins(co->prog, GSC_JUMP_IF_FALSE, 0, 0, -1, 0.0);
    body_jump = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);
    /* step */
    step_ip = co->prog->ncode;
    skip_ws(c);
    save = c->p;
    if (c->p < c->end && *c->p != ')') {
        if (!read_ident(c, iname, sizeof(iname))) {
            /* nothing to compile */
        } else if (compile_lhs(co, c, iname, save, 0) < 0) {
            co->prog->code[jif].a = co->prog->ncode;
            co->prog->code[body_jump].a = co->prog->ncode;
            return -1;
        }
    }
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;
    emit_ins(co->prog, GSC_JUMP_BACK, cond_ip, 0, -1, 0.0);
    co->prog->code[body_jump].a = co->prog->ncode;

    if (co->nloops < GSC_MAX_LOOPS) {
        ctx = &co->loops[co->nloops++];
        ctx->kind = 0;
        ctx->nbreaks = 0;
        ctx->ncontinues = 0;
    }
    if (compile_body_stmt(co, c) < 0) {
        co->prog->code[jif].a = co->prog->ncode;
        if (ctx) {
            for (k = 0; k < ctx->nbreaks; k++)
                co->prog->code[ctx->breaks[k]].a = co->prog->ncode;
            for (k = 0; k < ctx->ncontinues; k++)
                co->prog->code[ctx->continues[k]].a = co->prog->ncode;
            co->nloops--;
        }
        return -1;
    }
    if (ctx) {
        for (k = 0; k < ctx->ncontinues; k++)
            co->prog->code[ctx->continues[k]].a = step_ip;
    }
    emit_ins(co->prog, GSC_JUMP_BACK, step_ip, 0, -1, 0.0);
    co->prog->code[jif].a = co->prog->ncode;
    if (ctx) {
        for (k = 0; k < ctx->nbreaks; k++)
            co->prog->code[ctx->breaks[k]].a = co->prog->ncode;
        co->nloops--;
    }
    return 0;
}

typedef struct SwCase {
    int ival;
    int sidx;      /* string index, or -1 for int cases */
    int is_default;
    int patch;     /* code slot of the test's JUMP-to-body (-1 for default) */
    int body_ip;   /* -1 until the body is compiled */
} SwCase;

static int compile_switch(Comp* co, Cur* c) {
    SwCase cases[GSC_MAX_CASES];
    int ncases = 0;
    int sw_slot;
    int default_test_jump;
    int default_body = -1;
    LoopCtx* ctx = NULL;
    Cur scan;
    int depth;
    int i;
    int k;

    skip_ws(c);
    if (c->p < c->end && *c->p == '(') c->p++;
    if (compile_expr(co, c) < 0) return -1;
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;

    sw_slot = scratch_slot(co);
    emit_ins(co->prog, GSC_SET_LOCAL, sw_slot, 0, -1, 0.0);

    skip_ws(c);
    if (c->p >= c->end || *c->p != '{') return 0;
    c->p++;

    /* Scan the body (first pass) for top-level case/default labels. */
    scan = *c;
    depth = 1;
    while (scan.p < scan.end && depth > 0 && ncases < GSC_MAX_CASES) {
        skip_ws(&scan);
        if (scan.p >= scan.end) break;
        if (*scan.p == '"') {
            scan.p++;
            while (scan.p < scan.end && *scan.p != '"') scan.p++;
            if (scan.p < scan.end) scan.p++;
            continue;
        }
        if (*scan.p == '{') { depth++; scan.p++; continue; }
        if (*scan.p == '}') {
            depth--;
            if (depth == 0) break;
            scan.p++;
            continue;
        }
        if (depth == 1 && (isalpha((unsigned char)*scan.p) || *scan.p == '_')) {
            char tok[16];
            if (read_ident(&scan, tok, sizeof(tok))) {
                if (strcmp(tok, "case") == 0) {
                    SwCase* cc = &cases[ncases];
                    cc->is_default = 0;
                    cc->sidx = -1;
                    cc->ival = 0;
                    cc->patch = -1;
                    cc->body_ip = -1;
                    skip_ws(&scan);
                    if (scan.p < scan.end && *scan.p == '"') {
                        const char* ss;
                        scan.p++;
                        ss = scan.p;
                        while (scan.p < scan.end && *scan.p != '"') scan.p++;
                        cc->sidx = intern_str(co, ss, (size_t)(scan.p - ss));
                        if (scan.p < scan.end) scan.p++;
                    } else {
                        int sign = 1;
                        char buf[32];
                        size_t n = 0;
                        unsigned long long v;
                        if (scan.p < scan.end && *scan.p == '-') { sign = -1; scan.p++; }
                        while (scan.p < scan.end &&
                               isdigit((unsigned char)*scan.p) &&
                               n + 1 < sizeof(buf)) {
                            buf[n++] = *scan.p++;
                        }
                        buf[n] = '\0';
                        v = strtoull(buf, NULL, 10);
                        if (sign == -1) v = 0ULL - v;
                        cc->ival = (int)(int32_t)(uint32_t)v;
                    }
                    skip_ws(&scan);
                    if (scan.p < scan.end && *scan.p == ':') scan.p++;
                    ncases++;
                    continue;
                } else if (strcmp(tok, "default") == 0) {
                    SwCase* cc = &cases[ncases];
                    cc->is_default = 1;
                    cc->sidx = -1;
                    cc->ival = 0;
                    cc->patch = -1;
                    cc->body_ip = -1;
                    skip_ws(&scan);
                    if (scan.p < scan.end && *scan.p == ':') scan.p++;
                    ncases++;
                    continue;
                }
                /* Any other identifier (e.g. `showcase`, `lowercase`,
                 * `select`): the cursor is already past it. Do NOT rewind
                 * and then step one byte, or a name that merely contains
                 * `case` or `default` as a substring would register a
                 * phantom label. */
                continue;
            }
        }
        if (scan.p < scan.end) scan.p++;
    }

    /* Emit the test chain. Each test jumps to its body target slot. */
    for (i = 0; i < ncases; i++) {
        SwCase* cc = &cases[i];
        int jif;
        if (cc->is_default) continue;
        emit_ins(co->prog, GSC_GET_LOCAL, sw_slot, 0, -1, 0.0);
        if (cc->sidx >= 0) {
            emit_ins(co->prog, GSC_PUSH_STR, 0, 0, cc->sidx, 0.0);
        } else {
            emit_ins(co->prog, GSC_PUSH_INT, cc->ival, 0, -1, 0.0);
        }
        emit_ins(co->prog, GSC_EQ, 0, 0, -1, 0.0);
        jif = emit_ins(co->prog, GSC_JUMP_IF_FALSE, 0, 0, -1, 0.0);
        cc->patch = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);
        co->prog->code[jif].a = co->prog->ncode;
    }
    /* After all tests, jump to default body or end of switch. */
    default_test_jump = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);

    /* Push a switch context for break patches. */
    if (co->nloops < GSC_MAX_LOOPS) {
        ctx = &co->loops[co->nloops++];
        ctx->kind = 1;
        ctx->nbreaks = 0;
        ctx->ncontinues = 0;
    }

    /* Compile bodies in source order (second pass over the switch body). */
    {
        Cur bc = *c;
        int cur_case = 0;
        depth = 1;
        while (bc.p < bc.end && depth > 0) {
            skip_ws(&bc);
            if (bc.p >= bc.end) break;
            if (*bc.p == '}') {
                depth--;
                if (depth == 0) { bc.p++; break; }
                bc.p++;
                continue;
            }
            if (depth == 1 && (isalpha((unsigned char)*bc.p) || *bc.p == '_')) {
                char tok[16];
                Cur save = bc;
                if (read_ident(&bc, tok, sizeof(tok))) {
                    if (strcmp(tok, "case") == 0) {
                        skip_ws(&bc);
                        if (bc.p < bc.end && *bc.p == '"') {
                            bc.p++;
                            while (bc.p < bc.end && *bc.p != '"') bc.p++;
                            if (bc.p < bc.end) bc.p++;
                        } else {
                            if (bc.p < bc.end && *bc.p == '-') bc.p++;
                            while (bc.p < bc.end && isdigit((unsigned char)*bc.p)) bc.p++;
                        }
                        skip_ws(&bc);
                        if (bc.p < bc.end && *bc.p == ':') bc.p++;
                        if (cur_case < ncases) {
                            cases[cur_case].body_ip = co->prog->ncode;
                            cur_case++;
                        }
                        continue;
                    } else if (strcmp(tok, "default") == 0) {
                        skip_ws(&bc);
                        if (bc.p < bc.end && *bc.p == ':') bc.p++;
                        if (cur_case < ncases) {
                            cases[cur_case].body_ip = co->prog->ncode;
                            cur_case++;
                        }
                        continue;
                    } else {
                        bc = save;
                    }
                }
            }
            compile_stmt_keep(co, &bc);
        }
        *c = bc;
    }

    /* Patch jumps. */
    for (i = 0; i < ncases; i++) {
        SwCase* cc = &cases[i];
        if (cc->is_default) {
            default_body = cc->body_ip;
            continue;
        }
        {
            int target = cc->body_ip >= 0 ? cc->body_ip : co->prog->ncode;
            if (cc->patch >= 0) co->prog->code[cc->patch].a = target;
        }
    }
    co->prog->code[default_test_jump].a =
        (default_body >= 0) ? default_body : co->prog->ncode;

    if (ctx) {
        for (k = 0; k < ctx->nbreaks; k++)
            co->prog->code[ctx->breaks[k]].a = co->prog->ncode;
        co->nloops--;
    }
    return 0;
}

static int compile_if(Comp* co, Cur* c) {
    int jmp_false;
    const char* save2;
    char kw[16];
    skip_ws(c);
    if (c->p < c->end && *c->p == '(') c->p++;
    if (compile_expr(co, c) < 0) return -1;
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;
    jmp_false = emit_ins(co->prog, GSC_JUMP_IF_FALSE, 0, 0, -1, 0.0);
    if (compile_block(co, c) < 0) {
        /* The then-branch failed partway through. Pin jmp_false to the
         * instruction after whatever did make it in so no later run of
         * this program jumps to instruction 0. */
        co->prog->code[jmp_false].a = co->prog->ncode;
        return -1;
    }
    skip_ws(c);
    save2 = c->p;
    if (read_ident(c, kw, sizeof(kw)) && strcmp(kw, "else") == 0) {
        int jmp_end = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);
        co->prog->code[jmp_false].a = co->prog->ncode;
        if (compile_block(co, c) < 0) {
            /* Same for the else branch: pin jmp_end before bailing out. */
            co->prog->code[jmp_end].a = co->prog->ncode;
            return -1;
        }
        co->prog->code[jmp_end].a = co->prog->ncode;
    } else {
        c->p = save2;
        co->prog->code[jmp_false].a = co->prog->ncode;
    }
    return 0;
}

/* Argument list shared by a local thread. Leaves the cursor after ')'. */
static int compile_arg_list(Comp* co, Cur* c, int* argc_out) {
    int argc = 0;
    skip_ws(c);
    if (!(c->p < c->end && *c->p == '(')) return -1;
    c->p++;
    skip_ws(c);
    if (!(c->p < c->end && *c->p == ')')) {
        for (;;) {
            if (compile_expr(co, c) < 0) return -1;
            argc++;
            skip_ws(c);
            if (c->p < c->end && *c->p == ',') { c->p++; continue; }
            break;
        }
    }
    skip_ws(c);
    if (c->p < c->end && *c->p == ')') c->p++;
    *argc_out = argc;
    return 0;
}

/* thread name(); and thread path::name(); run once, inline. The statement
 * discards the value a normal return pushes. A GSC_STOP inside the thread
 * resumes at that discard as well. */
static int compile_thread(Comp* co, Cur* c) {
    char name[64];
    int at;
    skip_ws(c);
    if (!read_ident(c, name, sizeof(name))) {
        skip_to_semi(c);
        return 0;
    }
    skip_ws(c);
    at = co->prog->ncode;
    if (starts_far_call(c)) {
        int i;
        if (compile_far_call(co, c, name) < 0) return -1;
        for (i = co->prog->ncode - 1; i >= at; i--) {
            if (co->prog->code[i].op == GSC_FAR) {
                co->prog->code[i].op = (unsigned char)GSC_THREAD;
                break;
            }
        }
    } else if (c->p < c->end && *c->p == '(') {
        int argc = 0;
        int fi = find_func_index(co->prog, name);
        if (compile_arg_list(co, c, &argc) < 0) return -1;
        emit_ins(co->prog, GSC_THREAD, fi, argc, -1, 0.0);
    } else {
        skip_to_semi(c);
        return 0;
    }
    emit_ins(co->prog, GSC_SET_LOCAL, scratch_slot(co), 0, -1, 0.0);
    skip_ws(c);
    if (c->p < c->end && *c->p == ';') c->p++;
    return 0;
}

/* self method(), self thread name(), and self path::name() are entity
 * calls. They stop the branch. Anything else that starts with self is
 * skipped, the same way an unknown statement is. */
static int self_call_stmt(const Cur* c) {
    Cur look = *c;
    char method[64];
    skip_ws(&look);
    if (look.p >= look.end) return 0;
    if (*look.p == '.') {
        look.p++;
        if (!read_ident(&look, method, sizeof(method))) return 0;
        skip_ws(&look);
        return look.p < look.end && *look.p == '(';
    }
    if (!read_ident(&look, method, sizeof(method))) return 0;
    skip_ws(&look);
    if (strcmp(method, "thread") == 0) return 1;
    if (look.p < look.end && *look.p == '(') return 1;
    return starts_far_call(&look);
}

static int compile_body_stmt(Comp* co, Cur* c) {
    skip_ws(c);
    if (c->p < c->end && *c->p == '{') return compile_block(co, c);
    return compile_stmt(co, c);
}

static int compile_stmt(Comp* co, Cur* c) {
    char name[64];
    const char* save;
    skip_ws(c);
    if (c->p >= c->end) return 0;
    if (*c->p == '{') return compile_block(co, c);
    if (*c->p == ';') { c->p++; return 0; }
    save = c->p;
    if (!read_ident(c, name, sizeof(name))) {
        skip_to_semi(c);
        return 0;
    }
    if (strcmp(name, "return") == 0) {
        skip_ws(c);
        if (c->p < c->end && *c->p == ';') {
            c->p++;
            emit_ins(co->prog, GSC_PUSH_UNDEF, 0, 0, -1, 0.0);
            emit_ins(co->prog, GSC_RETURN, 0, 0, -1, 0.0);
            return 0;
        }
        if (compile_expr(co, c) < 0) return -1;
        skip_ws(c);
        if (c->p < c->end && *c->p == ';') c->p++;
        emit_ins(co->prog, GSC_RETURN, 0, 0, -1, 0.0);
        return 0;
    }
    if (strcmp(name, "if") == 0) return compile_if(co, c);
    if (strcmp(name, "while") == 0) return compile_while(co, c);
    if (strcmp(name, "for") == 0) return compile_for(co, c);
    if (strcmp(name, "switch") == 0) return compile_switch(co, c);
    if (strcmp(name, "break") == 0) {
        skip_ws(c);
        if (c->p < c->end && *c->p == ';') c->p++;
        if (co->nloops > 0) {
            LoopCtx* top = &co->loops[co->nloops - 1];
            int j = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);
            if (top->nbreaks < GSC_MAX_LABEL_PATCHES)
                top->breaks[top->nbreaks++] = j;
        }
        return 0;
    }
    if (strcmp(name, "continue") == 0) {
        int i;
        skip_ws(c);
        if (c->p < c->end && *c->p == ';') c->p++;
        for (i = co->nloops - 1; i >= 0; i--) {
            if (co->loops[i].kind == 0) {
                int j = emit_ins(co->prog, GSC_JUMP, 0, 0, -1, 0.0);
                if (co->loops[i].ncontinues < GSC_MAX_LABEL_PATCHES)
                    co->loops[i].continues[co->loops[i].ncontinues++] = j;
                break;
            }
        }
        return 0;
    }
    if (strcmp(name, "else") == 0) {
        /* Stray else: skip its body. */
        skip_ws(c);
        if (c->p < c->end && *c->p == '{') {
            const char* after = match_brace(c->p, c->end);
            c->p = after ? after : c->end;
        } else {
            skip_to_semi(c);
        }
        return 0;
    }
    if (strcmp(name, "level") == 0) return compile_level_assign(co, c);
    if (strcmp(name, "game") == 0) return compile_game_assign(co, c);
    if (strcmp(name, "thread") == 0) return compile_thread(co, c);
    if (strcmp(name, "wait") == 0 || strcmp(name, "waittill") == 0) {
        emit_ins(co->prog, GSC_STOP, 0, 0, -1, 0.0);
        skip_to_semi(c);
        return 0;
    }
    if (strcmp(name, "self") == 0) {
        if (self_call_stmt(c)) emit_ins(co->prog, GSC_STOP, 0, 0, -1, 0.0);
        skip_to_semi(c);
        return 0;
    }
    return compile_lhs(co, c, name, save, 1);
}

/* -------- function discovery -------- */

static int find_funcs(const char* text, size_t n, FuncDesc* out, int max) {
    const char* p = text;
    const char* end = text + n;
    int cnt = 0;
    while (p < end && cnt < max) {
        char name[64];
        Cur c;
        const char* paren_open;
        const char* paren_close;
        const char* body;
        const char* after;
        c.p = p; c.end = end;
        skip_ws(&c);
        if (c.p >= end) break;
        if (!read_ident(&c, name, sizeof(name))) { p = c.p + 1; continue; }
        {
            Cur q = c;
            skip_ws(&q);
            if (q.p >= end || *q.p != '(') { p = c.p; continue; }
            paren_open = q.p;
        }
        paren_close = memchr(paren_open, ')', (size_t)(end - paren_open));
        if (!paren_close) break;
        {
            Cur bc;
            bc.p = paren_close + 1; bc.end = end;
            skip_ws(&bc);
            if (bc.p >= end || *bc.p != '{') { p = c.p; continue; }
            body = bc.p;
        }
        after = match_brace(body, end);
        if (!after) break;
        snprintf(out[cnt].name, sizeof(out[cnt].name), "%s", name);
        out[cnt].params = paren_open + 1;
        out[cnt].params_len = (size_t)(paren_close - (paren_open + 1));
        out[cnt].body = body + 1;
        out[cnt].body_len = (size_t)((after - 1) - (body + 1));
        cnt++;
        p = after;
    }
    return cnt;
}

/* -------- public API -------- */

void gsc_prog_free(GscProg* p) {
    if (!p) return;
    free(p->code);
    free(p->funcs);
    free(p->strs);
    free(p->stroff);
    memset(p, 0, sizeof(*p));
}

int gsc_compile(const char* source, size_t size, GscProg* prog) {
    char* text;
    size_t w = 0;
    size_t i;
    FuncDesc funcs[GSC_MAX_FUNCS];
    int nf;
    int fi;
    Comp co;

    if (!source || !prog) return 0;
    memset(prog, 0, sizeof(*prog));
    prog->fuel = 100000;

    text = (char*)malloc(size + 1);
    if (!text) return 0;
    for (i = 0; i < size; i++) {
        if (source[i] == '/' && i + 1 < size && source[i + 1] == '/') {
            while (i < size && source[i] != '\n') i++;
            if (i < size) text[w++] = '\n';
            continue;
        }
        text[w++] = source[i];
    }
    text[w] = '\0';

    nf = find_funcs(text, w, funcs, GSC_MAX_FUNCS);

    /* First pass: register names so inner calls resolve. */
    if (nf > 0) {
        prog->funcs = (GscFunc*)calloc((size_t)nf, sizeof(GscFunc));
        if (!prog->funcs) { free(text); return 0; }
        prog->fcap = nf;
    }
    for (fi = 0; fi < nf; fi++) {
        snprintf(prog->funcs[fi].name, sizeof(prog->funcs[fi].name), "%s", funcs[fi].name);
        prog->funcs[fi].entry = -1;
        prog->funcs[fi].nparams = 0;
        prog->funcs[fi].nlocals = 0;
        prog->nfunc++;
    }

    /* Second pass: compile each body. */
    co.prog = prog;
    co.strs_cap = 0;
    co.strs_len = 0;
    for (fi = 0; fi < nf; fi++) {
        char pname[64];
        Cur pc;
        Cur bc;
        memset(co.locals, 0, sizeof(co.locals));
        co.nlocals = 0;
        co.scratch = -1;
        co.nloops = 0;
        pc.p = funcs[fi].params;
        pc.end = funcs[fi].params + funcs[fi].params_len;
        for (;;) {
            skip_ws(&pc);
            if (pc.p >= pc.end) break;
            if (!read_ident(&pc, pname, sizeof(pname))) break;
            local_add(&co, pname);
            skip_ws(&pc);
            if (pc.p < pc.end && *pc.p == ',') pc.p++;
        }
        prog->funcs[fi].nparams = co.nlocals;
        prog->funcs[fi].entry = prog->ncode;
        bc.p = funcs[fi].body;
        bc.end = funcs[fi].body + funcs[fi].body_len;
        while (bc.p < bc.end) {
            skip_ws(&bc);
            if (bc.p >= bc.end) break;
            if (*bc.p == '}') break;
            compile_stmt_keep(&co, &bc);
        }
        /* Fallthrough: implicit `return undefined;`. */
        emit_ins(prog, GSC_PUSH_UNDEF, 0, 0, -1, 0.0);
        emit_ins(prog, GSC_RETURN, 0, 0, -1, 0.0);
        prog->funcs[fi].nlocals = co.nlocals;
    }

    free(text);
    return 1;
}
