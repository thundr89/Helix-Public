/*
 * Tokenized CoD GSC runtime frontend. GPL-2.0-or-later.
 * Copyright (C) 2026 Helix-Public contributors.
 * One lexical pass, signature discovery, then direct bytecode generation.
 * No recovery by skipping statements: an error discards the entire program.
 */
#include "gsc/compile.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    T_ID = 256,
    T_INT,
    T_FLOAT,
    T_STRING,
    T_EQ,
    T_NE,
    T_LE,
    T_GE,
    T_AND,
    T_OR,
    T_SHL,
    T_SHR,
    T_INC,
    T_DEC,
    T_ADDSET,
    T_SUBSET,
    T_MULSET,
    T_DIVSET,
    T_MODSET,
    T_ANDSET,
    T_ORSET,
    T_XORSET,
    T_SHLSET,
    T_SHRSET,
    T_SCOPE,
    T_DEVBEGIN,
    T_DEVEND
};
enum { E_VALUE, E_LOCAL, E_GLOBAL, E_FIELD, E_INDEX, E_SYMBOL, E_POINTER };
enum {
    MAX_SOURCE = 16 * 1024 * 1024,
    MAX_TOKENS = 2000000,
    MAX_CODE = 1000000,
    MAX_FUNCTIONS = 8192,
    MAX_LOCALS = 1024,
    MAX_ARGS = 128,
    MAX_NESTING = 128
};
typedef struct Token {
    int kind, id, integer;
    double number;
    size_t offset;
    unsigned line, column;
} Token;
typedef struct Signature {
    int name, params, body, end, developer, animtree;
} Signature;
typedef struct Flow {
    int breaks, continues, is_loop, continue_target;
} Flow;
typedef struct RefPatch {
    int at, previous;
} RefPatch;
typedef struct Expr {
    int kind, a, statement_call, patch;
} Expr;
typedef struct Front {
    GscProg *p;
    GscCompileDiagnostic *error;
    const char *source;
    size_t size;
    Token *tokens;
    int ntokens, tcap, pos;
    int *hashes;
    int hcap;
    int stringbytes, stringcap;
    Signature *signatures;
    int fn, developer, animtree;
    int *functions_by_name;
    int function_names;
    unsigned char *reserved_names;
    RefPatch *references;
    int nreferences, referencecap;
    int locals[MAX_LOCALS], localhash[MAX_LOCALS * 2], nlocals, temp;
    Flow flows[MAX_NESTING];
    int nflows, depth, failed;
} Front;

static int fail_at(Front *c, const Token *t, const char *message) {
    if (!c->failed && c->error) {
        c->error->offset = t ? t->offset : c->size;
        c->error->line = t ? t->line : 1;
        c->error->column = t ? t->column : 1;
        snprintf(c->error->message, sizeof(c->error->message), "%s", message);
    }
    c->failed = 1;
    return 0;
}
static Token *token(Front *c) { return &c->tokens[c->pos < c->ntokens ? c->pos : c->ntokens - 1]; }
static int fail(Front *c, const char *m) { return fail_at(c, token(c), m); }
static const char *text(Front *c, int id) { return c->p->strs + c->p->stroff[id]; }
static uint32_t hash_bytes(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}
static int rehash(Front *c, int cap) {
    int *slots = (int *)calloc((size_t)cap, sizeof(int));
    int i;
    if (!slots)
        return fail_at(c, NULL, "out of memory in symbol table");
    for (i = 0; i < c->p->nstr; i++) {
        const char *s = text(c, i);
        unsigned k = hash_bytes(s, strlen(s)) & (unsigned)(cap - 1);
        while (slots[k])
            k = (k + 1) & (unsigned)(cap - 1);
        slots[k] = i + 1;
    }
    free(c->hashes);
    c->hashes = slots;
    c->hcap = cap;
    return 1;
}
static int intern(Front *c, const char *s, size_t n) {
    GscProg *p = c->p;
    unsigned k;
    int id;
    if (n > MAX_SOURCE) {
        fail_at(c, NULL, "string exceeds source limit");
        return -1;
    }
    if (!c->hcap || (p->nstr + 1) * 2 >= c->hcap)
        if (!rehash(c, c->hcap ? c->hcap * 2 : 256))
            return -1;
    k = hash_bytes(s, n) & (unsigned)(c->hcap - 1);
    while (c->hashes[k]) {
        id = c->hashes[k] - 1;
        if (strlen(text(c, id)) == n && !memcmp(text(c, id), s, n))
            return id;
        k = (k + 1) & (unsigned)(c->hcap - 1);
    }
    if (p->nstr >= p->scap) {
        int cap = p->scap ? p->scap * 2 : 128;
        int *a = (int *)realloc(p->stroff, (size_t)cap * sizeof(int));
        if (!a) {
            fail_at(c, NULL, "out of memory in strings");
            return -1;
        }
        p->stroff = a;
        p->scap = cap;
    }
    if (n > (size_t)(INT_MAX - c->stringbytes - 1)) {
        fail_at(c, NULL, "string pool limit");
        return -1;
    }
    if (c->stringbytes + (int)n + 1 > c->stringcap) {
        int cap = c->stringcap ? c->stringcap : 1024;
        char *a;
        while (cap < c->stringbytes + (int)n + 1) {
            if (cap > INT_MAX / 2) {
                fail_at(c, NULL, "string pool limit");
                return -1;
            }
            cap *= 2;
        }
        a = (char *)realloc(p->strs, (size_t)cap);
        if (!a) {
            fail_at(c, NULL, "out of memory in string pool");
            return -1;
        }
        p->strs = a;
        c->stringcap = cap;
    }
    id = p->nstr++;
    p->stroff[id] = c->stringbytes;
    memcpy(p->strs + c->stringbytes, s, n);
    p->strs[c->stringbytes + n] = 0;
    c->stringbytes += (int)n + 1;
    c->hashes[k] = id + 1;
    return id;
}
static int add_token(Front *c, Token t) {
    if (c->ntokens >= MAX_TOKENS)
        return fail_at(c, &t, "token limit exceeded");
    if (c->ntokens >= c->tcap) {
        int cap = c->tcap ? c->tcap * 2 : 512;
        Token *p = (Token *)realloc(c->tokens, (size_t)cap * sizeof(Token));
        if (!p)
            return fail_at(c, &t, "out of memory in tokens");
        c->tokens = p;
        c->tcap = cap;
    }
    c->tokens[c->ntokens++] = t;
    return 1;
}
static int lex(Front *c) {
    size_t i = 0;
    unsigned line = 1, column = 1;
    const char *s = c->source;
    while (i < c->size && !c->failed) {
        Token t;
        unsigned char ch = (unsigned char)s[i];
        size_t start = i;
        memset(&t, 0, sizeof(t));
        t.offset = i;
        t.line = line;
        t.column = column;
        t.id = -1;
        if (isspace(ch)) {
            if (ch == '\n') {
                line++;
                column = 1;
            } else
                column++;
            i++;
            continue;
        }
        if (ch == '/' && i + 1 < c->size && s[i + 1] == '/') {
            while (i < c->size && s[i] != '\n') {
                i++;
                column++;
            }
            continue;
        }
        if (ch == '/' && i + 1 < c->size && s[i + 1] == '*') {
            i += 2;
            column += 2;
            while (i + 1 < c->size && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') {
                    line++;
                    column = 1;
                } else
                    column++;
                i++;
            }
            if (i + 1 >= c->size)
                return fail_at(c, &t, "unterminated block comment");
            i += 2;
            column += 2;
            continue;
        }
        if (isalpha(ch) || ch == '_') {
            char name[64];
            size_t n = 0;
            while (i < c->size && (isalnum((unsigned char)s[i]) || s[i] == '_')) {
                if (n + 1 >= sizeof(name))
                    return fail_at(c, &t, "identifier exceeds 63 bytes");
                name[n++] = (char)tolower((unsigned char)s[i++]);
                column++;
            }
            t.kind = T_ID;
            t.id = intern(c, name, n);
        } else if (isdigit(ch) ||
                   (ch == '.' && i + 1 < c->size && isdigit((unsigned char)s[i + 1]))) {
            char number[128];
            size_t n;
            int floating = 0;
            if (ch == '0' && i + 1 < c->size && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i += 2;
                while (i < c->size && isxdigit((unsigned char)s[i]))
                    i++;
                if (i == start + 2)
                    return fail_at(c, &t, "hexadecimal literal requires digits");
            } else {
                while (i < c->size && isdigit((unsigned char)s[i]))
                    i++;
                if (i < c->size && s[i] == '.') {
                    floating = 1;
                    i++;
                    while (i < c->size && isdigit((unsigned char)s[i]))
                        i++;
                }
                if (i < c->size && (s[i] == 'e' || s[i] == 'E')) {
                    size_t digits;
                    floating = 1;
                    i++;
                    if (i < c->size && (s[i] == '+' || s[i] == '-'))
                        i++;
                    digits = i;
                    while (i < c->size && isdigit((unsigned char)s[i]))
                        i++;
                    if (i == digits)
                        return fail_at(c, &t, "exponent requires digits");
                }
            }
            if (i < c->size && (isalpha((unsigned char)s[i]) || s[i] == '_'))
                return fail_at(c, &t, "invalid numeric literal");
            n = i - start;
            if (n >= sizeof(number))
                return fail_at(c, &t, "numeric literal too long");
            memcpy(number, s + start, n);
            number[n] = 0;
            column += (unsigned)n;
            t.kind = floating ? T_FLOAT : T_INT;
            if (floating) {
                char *end;
                errno = 0;
                t.number = strtod(number, &end);
                if (*end || errno == ERANGE)
                    return fail_at(c, &t, "floating literal out of range");
            } else {
                uint32_t value = 0;
                size_t k =
                    (n > 1 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) ? 2 : 0;
                unsigned base = k ? 16u : 10u;
                for (; k < n; k++) {
                    unsigned digit = (unsigned)(isdigit((unsigned char)number[k])
                                                    ? number[k] - '0'
                                                    : tolower((unsigned char)number[k]) - 'a' + 10);
                    value = value * base + digit;
                }
                memcpy(&t.integer, &value, sizeof(value));
            }
        } else if (ch == '"') {
            char *value = (char *)malloc(c->size - i);
            size_t n = 0;
            if (!value)
                return fail_at(c, &t, "out of memory in string literal");
            i++;
            column++;
            while (i < c->size && s[i] != '"') {
                char v = s[i++];
                column++;
                if (v == '\n' || v == '\r') {
                    free(value);
                    return fail_at(c, &t, "newline in string literal");
                }
                if (v == '\\' && i < c->size) {
                    char e = s[i];
                    if (e == 'n' || e == 't' || e == 'r' || e == '"' || e == '\\') {
                        i++;
                        column++;
                        v = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                    }
                }
                if (!v) {
                    free(value);
                    return fail_at(c, &t, "NUL in string literal");
                }
                value[n++] = v;
            }
            if (i >= c->size) {
                free(value);
                return fail_at(c, &t, "unterminated string literal");
            }
            i++;
            column++;
            t.kind = T_STRING;
            t.id = intern(c, value, n);
            free(value);
        } else {
            static const struct {
                const char *spelling;
                int kind;
            } ops[] = {{"<<=", T_SHLSET}, {">>=", T_SHRSET},  {"==", T_EQ},     {"!=", T_NE},
                       {"<=", T_LE},      {">=", T_GE},       {"&&", T_AND},    {"||", T_OR},
                       {"<<", T_SHL},     {">>", T_SHR},      {"++", T_INC},    {"--", T_DEC},
                       {"+=", T_ADDSET},  {"-=", T_SUBSET},   {"*=", T_MULSET}, {"/=", T_DIVSET},
                       {"%=", T_MODSET},  {"&=", T_ANDSET},   {"|=", T_ORSET},  {"^=", T_XORSET},
                       {"::", T_SCOPE},   {"/#", T_DEVBEGIN}, {"#/", T_DEVEND}};
            size_t k;
            int found = 0;
            for (k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
                size_t n = strlen(ops[k].spelling);
                if (n <= c->size - i && !memcmp(s + i, ops[k].spelling, n)) {
                    t.kind = ops[k].kind;
                    i += n;
                    column += (unsigned)n;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (!ch || !strchr("{}()[];,:.?+-*/%&|^~!<>=#\\", ch))
                    return fail_at(c, &t, "invalid character");
                t.kind = ch;
                i++;
                column++;
            }
        }
        if (c->failed || !add_token(c, t))
            return 0;
    }
    {
        Token eof;
        memset(&eof, 0, sizeof(eof));
        eof.offset = i;
        eof.line = line;
        eof.column = column;
        return add_token(c, eof);
    }
}
static int kind(Front *c) { return token(c)->kind; }
static int accept(Front *c, int k) {
    if (kind(c) == k) {
        c->pos++;
        return 1;
    }
    return 0;
}
static int expect(Front *c, int k, const char *m) { return accept(c, k) ? 1 : fail(c, m); }
static int word(Front *c, const char *s) {
    return kind(c) == T_ID && !strcmp(text(c, token(c)->id), s);
}
static int take_word(Front *c, const char *s) {
    if (word(c, s)) {
        c->pos++;
        return 1;
    }
    return 0;
}
static int emit(Front *c, int op, int a, int b, int str, double f) {
    GscProg *p = c->p;
    GscIns *i;
    int at;
    if (c->failed)
        return -1;
    if (p->ncode >= MAX_CODE) {
        fail(c, "bytecode limit exceeded");
        return -1;
    }
    if (p->ncode >= p->cap) {
        int cap = p->cap ? p->cap * 2 : 512;
        GscIns *a2 = (GscIns *)realloc(p->code, (size_t)cap * sizeof(GscIns));
        if (!a2) {
            fail(c, "out of memory in bytecode");
            return -1;
        }
        p->code = a2;
        p->cap = cap;
    }
    at = p->ncode++;
    i = &p->code[at];
    memset(i, 0, sizeof(*i));
    i->op = (unsigned char)op;
    i->a = a;
    i->b = b;
    i->str = str;
    i->f = f;
    i->line = token(c)->line;
    i->column = token(c)->column;
    return at;
}
static int op(Front *c, int opcode) { return emit(c, opcode, 0, 0, -1, 0); }
static void patch(Front *c, int at, int target) {
    if (at >= 0)
        c->p->code[at].a = target;
}
static int local(Front *c, int id) {
    unsigned at = (uint32_t)id * 2654435761u & (MAX_LOCALS * 2u - 1u);
    int slot;
    if (id < 0)
        return -1;
    if (id < c->function_names && c->reserved_names[id]) {
        fail(c, "reserved word cannot name a local variable or parameter");
        return -1;
    }
    while (c->localhash[at]) {
        slot = c->localhash[at] - 1;
        if (c->locals[slot] == id)
            return slot;
        at = (at + 1) & (MAX_LOCALS * 2u - 1u);
    }
    if (c->nlocals >= MAX_LOCALS) {
        fail(c, "local variable limit exceeded");
        return -1;
    }
    slot = c->nlocals++;
    c->locals[slot] = id;
    c->localhash[at] = slot + 1;
    return slot;
}
static int temporary(Front *c) {
    char name[32];
    snprintf(name, sizeof(name), "@temp%d", c->temp++);
    return local(c, intern(c, name, strlen(name)));
}
static int function(Front *c, int name) {
    return name >= 0 && name < c->function_names ? c->functions_by_name[name] : -1;
}
static int builtin(Front *c, int name) {
    static const struct {
        const char *name;
        int id;
    } names[] = {{"giveweapon", GSC_BU_GIVE},          {"takeweapon", GSC_BU_TAKE},
                 {"takeallweapons", GSC_BU_TAKE},      {"ambientplay", GSC_BU_AMBIENT},
                 {"spawnstruct", GSC_BU_STRUCT},       {"precachemodel", GSC_BU_PC_MODEL},
                 {"precachesound", GSC_BU_PC_SOUND},   {"precacheshader", GSC_BU_PC_SHADER},
                 {"precacheitem", GSC_BU_PC_WEAPON},   {"loadfx", GSC_BU_LOAD_FX},
                 {"setcvar", GSC_BU_SET_CVAR},         {"getent", GSC_BU_GETENT},
                 {"getentarray", GSC_BU_GETENTARRAY},  {"isdefined", GSC_BU_DEFINED},
                 {"getcvar", GSC_BU_GETCVAR},          {"getcvarint", GSC_BU_GETCVARINT},
                 {"getcvarfloat", GSC_BU_GETCVARFLOAT},
                 {"setdvar", GSC_BU_SET_CVAR},         {"getdvar", GSC_BU_GETCVAR},
                 {"getdvarint", GSC_BU_GETCVARINT},    {"getdvarfloat", GSC_BU_GETCVARFLOAT}};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(text(c, name), names[i].name))
            return names[i].id;
    return 0;
}
static Expr expr_value(void) {
    Expr e;
    memset(&e, 0, sizeof(e));
    e.kind = E_VALUE;
    e.patch = -1;
    return e;
}
static int force(Front *c, Expr *e) {
    int at = -1;
    if (e->kind == E_LOCAL || e->kind == E_SYMBOL)
        at = emit(c, GSC_GET_LOCAL, e->a, 0, -1, 0);
    else if (e->kind == E_GLOBAL)
        at = emit(c, GSC_GET_GLOBAL, e->a, 0, -1, 0);
    else if (e->kind == E_FIELD)
        at = emit(c, GSC_GET_FIELD, e->a, 0, -1, 0);
    else if (e->kind == E_INDEX)
        at = op(c, GSC_GET_INDEX);
    else if (e->kind == E_POINTER)
        return fail(c, "function pointer must be called");
    e->kind = E_VALUE;
    return at;
}
static int remember(Front *c, Expr *e, int at) {
    if (at < 0)
        return !c->failed;
    if (c->nreferences >= c->referencecap) {
        int cap = c->referencecap ? c->referencecap * 2 : 64;
        RefPatch *refs = (RefPatch *)realloc(c->references, (size_t)cap * sizeof(*refs));
        if (!refs)
            return fail(c, "out of memory in references");
        c->references = refs;
        c->referencecap = cap;
    }
    c->references[c->nreferences].at = at;
    c->references[c->nreferences].previous = e->patch;
    e->patch = c->nreferences++;
    return 1;
}
static void create_arrays(Front *c, Expr *e) {
    int i;
    for (i = e->patch; i >= 0; i = c->references[i].previous)
        c->p->code[c->references[i].at].b = 1;
}
static int store(Front *c, Expr *e) {
    if (e->kind == E_LOCAL || e->kind == E_SYMBOL)
        emit(c, GSC_SET_LOCAL, e->a, 0, -1, 0);
    else if (e->kind == E_FIELD)
        emit(c, GSC_SET_FIELD, e->a, 0, -1, 0);
    else if (e->kind == E_INDEX)
        op(c, GSC_SET_INDEX);
    else
        return fail(c, "assignment requires a variable, field or array element");
    return !c->failed;
}
static int read_for_update(Front *c, Expr *e) {
    if (e->kind == E_FIELD)
        op(c, GSC_DUP);
    else if (e->kind == E_INDEX)
        op(c, GSC_DUP2);
    else if (e->kind != E_LOCAL && e->kind != E_SYMBOL)
        return fail(c, "update requires a variable, field or array element");
    {
        Expr copy = *e;
        force(c, &copy);
    }
    return !c->failed;
}
static Expr expression(Front *c, int minprec);
static int statement(Front *c);
static int arguments(Front *c) {
    int n = 0;
    if (!expect(c, '(', "expected '('"))
        return 0;
    if (!accept(c, ')')) {
        do {
            Expr e = expression(c, 1);
            force(c, &e);
            if (++n > MAX_ARGS) {
                fail(c, "argument limit exceeded");
                return 0;
            }
        } while (accept(c, ','));
        expect(c, ')', "expected ')' after arguments");
    }
    return n;
}
static int far_ahead(Front *c) {
    int p = c->pos;
    if (c->tokens[p].kind != T_ID)
        return 0;
    p++;
    while (c->tokens[p].kind == '\\' || c->tokens[p].kind == '/') {
        p++;
        if (c->tokens[p].kind != T_ID)
            return 0;
        p++;
    }
    return c->tokens[p].kind == T_SCOPE;
}
static int path_name(Front *c, int *name) {
    char path[256];
    size_t n = 0;
    if (kind(c) != T_ID)
        return fail(c, "expected script path");
    for (;;) {
        const char *s = text(c, token(c)->id);
        size_t size = strlen(s);
        if (n + size + 5 >= sizeof(path))
            return fail(c, "script path exceeds 250 bytes");
        memcpy(path + n, s, size);
        n += size;
        c->pos++;
        if (kind(c) != '\\' && kind(c) != '/')
            break;
        path[n++] = '/';
        c->pos++;
        if (kind(c) != T_ID)
            return fail(c, "expected path component");
    }
    if (!expect(c, T_SCOPE, "expected '::'"))
        return -1;
    if (kind(c) != T_ID) {
        fail(c, "expected function name after '::'");
        return -1;
    }
    *name = token(c)->id;
    c->pos++;
    memcpy(path + n, ".gsc", 5);
    return intern(c, path, n + 4);
}
static Expr invoke(Front *c, int name, int path, int flags) {
    Expr out = expr_value();
    int n = arguments(c), fn = function(c, name), bi = builtin(c, name);
    out.statement_call = 1;
    if (path >= 0)
        emit(c,
             (flags & 2) ? ((flags & 1) ? GSC_ENTITY_THREAD : GSC_THREAD)
                         : ((flags & 1) ? GSC_ENTITY_FAR : GSC_FAR),
             path, name, n, 0);
    else if (fn >= 0)
        emit(c,
             (flags & 2) ? ((flags & 1) ? GSC_ENTITY_THREAD : GSC_THREAD)
                         : ((flags & 1) ? GSC_ENTITY_CALL : GSC_CALL),
             fn, n, -1, 0);
    else if (!flags && bi) {
        emit(c, GSC_BUILTIN, bi, n, -1, 0);
        if (!(bi == GSC_BU_STRUCT || bi == GSC_BU_LOAD_FX || bi >= GSC_BU_GETENT))
            op(c, GSC_PUSH_UNDEF);
    } else if ((flags & 1) && !(flags & 2)) {
        emit(c, GSC_METHOD, name, n, -1, 0);
    } else
        emit(c, GSC_NAMED_CALL, name, n, flags, 0);
    return out;
}
static Expr indirect(Front *c, int flags) {
    Expr out = expr_value(), target;
    out.statement_call = 1;
    expect(c, '[', "expected '[['");
    expect(c, '[', "expected '[['");
    target = expression(c, 1);
    force(c, &target);
    expect(c, ']', "expected ']]'");
    expect(c, ']', "expected ']]'");
    {
        int argc = arguments(c);
        emit(c, GSC_INDIRECT, flags, argc, -1, 0);
    }
    return out;
}
static Expr method(Front *c, Expr receiver, int threaded) {
    int name, path = -1;
    force(c, &receiver);
    if (kind(c) == '[' && c->tokens[c->pos + 1].kind == '[')
        return indirect(c, 1 | (threaded ? 2 : 0));
    if (far_ahead(c))
        path = path_name(c, &name);
    else {
        if (kind(c) != T_ID) {
            fail(c, "expected method name");
            return expr_value();
        }
        name = token(c)->id;
        c->pos++;
    }
    if (path < 0 && !threaded && !strcmp(text(c, name), "waittill")) {
        int outputs[MAX_ARGS], n = 0;
        Expr event;
        expect(c, '(', "expected '(' after waittill");
        event = expression(c, 1);
        force(c, &event);
        while (accept(c, ',')) {
            if (kind(c) != T_ID || n >= MAX_ARGS) {
                fail(c, "waittill outputs must be local variables");
                return expr_value();
            }
            outputs[n++] = local(c, token(c)->id);
            c->pos++;
        }
        expect(c, ')', "expected ')' after waittill outputs");
        emit(c, GSC_METHOD, name, 1, -1, 0);
        for (name = 0; name < n; name++)
            emit(c, GSC_EVENT_ARG, outputs[name], name, -1, 0);
        {
            Expr out = expr_value();
            out.statement_call = 1;
            return out;
        }
    }
    return invoke(c, name, path, 1 | (threaded ? 2 : 0));
}
static Expr primary(Front *c) {
    Expr e = expr_value();
    Token t = *token(c);
    int k = t.kind;
    if (c->failed)
        return e;
    if (++c->depth > MAX_NESTING) {
        fail(c, "expression nesting limit");
        c->depth--;
        return e;
    }
    c->pos++;
    if (k == T_INT)
        emit(c, GSC_PUSH_INT, t.integer, 0, -1, 0);
    else if (k == T_FLOAT)
        emit(c, GSC_PUSH_FLOAT, 0, 0, -1, t.number);
    else if (k == T_STRING)
        emit(c, GSC_PUSH_STR, 0, 0, t.id, 0);
    else if (k == '&') {
        if (kind(c) != T_STRING)
            fail(c, "localized literal requires a quoted string");
        else {
            emit(c, GSC_PUSH_ISTR, 0, 0, token(c)->id, 0);
            c->pos++;
        }
    } else if (k == '-' || k == '!' || k == '~' || k == '+') {
        e = primary(c);
        force(c, &e);
        e.statement_call = 0;
        if (k != '+')
            op(c, k == '-' ? GSC_NEG : k == '!' ? GSC_NOT : GSC_BITNOT);
    } else if (k == '(') {
        if (kind(c) == T_ID && c->tokens[c->pos + 1].kind == ')' &&
            (word(c, "int") || word(c, "float") || word(c, "string") || word(c, "bool"))) {
            int cast = word(c, "int")      ? GSC_CAST_INT
                       : word(c, "float")  ? GSC_CAST_FLOAT
                       : word(c, "string") ? GSC_CAST_STRING
                                           : GSC_CAST_BOOL;
            c->pos += 2;
            e = primary(c);
            force(c, &e);
            e.statement_call = 0;
            op(c, cast);
        } else {
            e = expression(c, 1);
            force(c, &e);
            if (accept(c, ',')) {
                Expr y = expression(c, 1), z;
                force(c, &y);
                expect(c, ',', "vector requires exactly three components");
                z = expression(c, 1);
                force(c, &z);
                op(c, GSC_VECTOR);
            }
            expect(c, ')', "expected ')' after expression");
        }
    } else if (k == '[') {
        if (kind(c) == '[') {
            c->pos--;
            e = indirect(c, 0);
        } else {
            expect(c, ']', "only [] is an array literal");
            op(c, GSC_NEW_ARRAY);
        }
    } else if (k == T_SCOPE) {
        if (kind(c) != T_ID)
            fail(c, "expected function reference name");
        else {
            int name = token(c)->id, fn = function(c, name);
            c->pos++;
            if (fn >= 0)
                emit(c, GSC_FUNC_REF, fn, 0, -1, 0);
            else
                emit(c, GSC_NAMED_REF, name, 0, -1, 0);
        }
    } else if (k == '#') {
        if (take_word(c, "animtree") && c->animtree >= 0)
            emit(c, GSC_ANIMTREE, c->animtree, 0, -1, 0);
        else
            fail(c, "#animtree requires #using_animtree");
    } else if (k == '%') {
        if (kind(c) != T_ID || c->animtree < 0)
            fail(c, "animation literal requires #using_animtree and a name");
        else {
            emit(c, GSC_ANIMATION, c->animtree, token(c)->id, -1, 0);
            c->pos++;
        }
    } else if (k == T_ID) {
        const char *s = text(c, t.id);
        if (!strcmp(s, "undefined"))
            op(c, GSC_PUSH_UNDEF);
        else if (!strcmp(s, "true") || !strcmp(s, "false"))
            emit(c, GSC_PUSH_INT, !strcmp(s, "true"), 0, -1, 0);
        else if (!strcmp(s, "thread")) {
            if (kind(c) == '[' && c->tokens[c->pos + 1].kind == '[')
                e = indirect(c, 2);
            else {
                int name, path = -1;
                if (far_ahead(c))
                    path = path_name(c, &name);
                else if (kind(c) == T_ID) {
                    name = token(c)->id;
                    c->pos++;
                } else {
                    fail(c, "expected function after thread");
                    name = -1;
                }
                if (!c->failed)
                    e = invoke(c, name, path, 2);
            }
        } else {
            c->pos--;
            if (far_ahead(c)) {
                int name, path = path_name(c, &name);
                if (kind(c) == '(')
                    e = invoke(c, name, path, 0);
                else
                    emit(c, GSC_FAR_REF, path, name, -1, 0);
            } else {
                c->pos++;
                if (kind(c) == '(')
                    e = invoke(c, t.id, -1, 0);
                else if (!strcmp(s, "self") || !strcmp(s, "level") || !strcmp(s, "game") ||
                         !strcmp(s, "anim")) {
                    e.kind = E_GLOBAL;
                    e.a = !strcmp(s, "self")    ? 0
                          : !strcmp(s, "level") ? 1
                          : !strcmp(s, "game")  ? 2
                                                : 3;
                } else {
                    e.kind = E_SYMBOL;
                    e.a = local(c, t.id);
                }
            }
        }
    } else
        fail_at(c, &t, "expected expression");
    while (!c->failed) {
        if (accept(c, '.')) {
            force(c, &e);
            if (kind(c) != T_ID) {
                fail(c, "expected field name");
                break;
            }
            if (c->tokens[c->pos + 1].kind == '(')
                e = method(c, expr_value(), 0);
            else {
                e.kind = E_FIELD;
                e.a = token(c)->id;
                c->pos++;
            }
        } else if (kind(c) == '[' && c->tokens[c->pos + 1].kind != '[') {
            Expr index;
            int at = force(c, &e);
            remember(c, &e, at);
            c->pos++;
            index = expression(c, 1);
            force(c, &index);
            expect(c, ']', "expected ']' after array index");
            e.kind = E_INDEX;
        } else if (word(c, "thread")) {
            c->pos++;
            e = method(c, e, 1);
        } else if (kind(c) == T_ID && (c->tokens[c->pos + 1].kind == '(' || far_ahead(c)))
            e = method(c, e, 0);
        else if (kind(c) == '[' && c->tokens[c->pos + 1].kind == '[') {
            force(c, &e);
            e = indirect(c, 1);
        } else
            break;
    }
    c->depth--;
    return e;
}
static int precedence(int k) {
    switch (k) {
    case T_OR:
        return 1;
    case T_AND:
        return 2;
    case '|':
        return 3;
    case '^':
        return 4;
    case '&':
        return 5;
    case T_EQ:
    case T_NE:
        return 6;
    case '<':
    case '>':
    case T_LE:
    case T_GE:
        return 7;
    case T_SHL:
    case T_SHR:
        return 8;
    case '+':
    case '-':
        return 9;
    case '*':
    case '/':
    case '%':
        return 10;
    default:
        return 0;
    }
}
static int binary_op(int k) {
    switch (k) {
    case '+':
        return GSC_ADD;
    case '-':
        return GSC_SUB;
    case '*':
        return GSC_MUL;
    case '/':
        return GSC_DIV;
    case '%':
        return GSC_MOD;
    case '<':
        return GSC_LT;
    case '>':
        return GSC_GT;
    case T_LE:
        return GSC_LE;
    case T_GE:
        return GSC_GE;
    case T_EQ:
    case T_NE:
        return GSC_EQ;
    case '&':
        return GSC_BITAND;
    case '|':
        return GSC_BITOR;
    case '^':
        return GSC_BITXOR;
    case T_SHL:
        return GSC_SHL;
    case T_SHR:
        return GSC_SHR;
    default:
        return GSC_NOP;
    }
}
static Expr expression(Front *c, int minprec) {
    Expr left = primary(c);
    int prec;
    while (!c->failed && (prec = precedence(kind(c))) >= minprec && prec) {
        int k = kind(c);
        c->pos++;
        force(c, &left);
        if (k == T_AND || k == T_OR) {
            int branch = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0), end;
            Expr right;
            if (k == T_OR) {
                emit(c, GSC_PUSH_INT, 1, 0, -1, 0);
                end = emit(c, GSC_JUMP, 0, 0, -1, 0);
                patch(c, branch, c->p->ncode);
                right = expression(c, prec + 1);
                force(c, &right);
                op(c, GSC_NOT);
                op(c, GSC_NOT);
                patch(c, end, c->p->ncode);
            } else {
                right = expression(c, prec + 1);
                force(c, &right);
                op(c, GSC_NOT);
                op(c, GSC_NOT);
                end = emit(c, GSC_JUMP, 0, 0, -1, 0);
                patch(c, branch, c->p->ncode);
                emit(c, GSC_PUSH_INT, 0, 0, -1, 0);
                patch(c, end, c->p->ncode);
            }
        } else {
            Expr right = expression(c, prec + 1);
            force(c, &right);
            op(c, binary_op(k));
            if (k == T_NE)
                op(c, GSC_NOT);
        }
        left = expr_value();
    }
    return left;
}
static int assignment(Front *c, int terminator) {
    Expr left = expression(c, 1);
    int k = kind(c), update = 0;
    if (k == '=' || k == T_INC || k == T_DEC || k == T_ADDSET || k == T_SUBSET || k == T_MULSET ||
        k == T_DIVSET || k == T_MODSET || k == T_ANDSET || k == T_ORSET || k == T_XORSET ||
        k == T_SHLSET || k == T_SHRSET) {
        c->pos++;
        create_arrays(c, &left);
        if (k != '=') {
            read_for_update(c, &left);
            update = 1;
        }
        if (k == T_INC || k == T_DEC)
            emit(c, GSC_PUSH_INT, 1, 0, -1, 0);
        else {
            Expr right = expression(c, 1);
            force(c, &right);
        }
        if (update) {
            int arithmetic = k == T_INC || k == T_ADDSET   ? '+'
                             : k == T_DEC || k == T_SUBSET ? '-'
                             : k == T_MULSET               ? '*'
                             : k == T_DIVSET               ? '/'
                             : k == T_MODSET               ? '%'
                             : k == T_ANDSET               ? '&'
                             : k == T_ORSET                ? '|'
                             : k == T_XORSET               ? '^'
                             : k == T_SHLSET               ? T_SHL
                                                           : T_SHR;
            op(c, binary_op(arithmetic));
        }
        store(c, &left);
    } else {
        if (!left.statement_call)
            return fail(c, "expected assignment or function call statement");
        force(c, &left);
        op(c, GSC_POP);
    }
    if (terminator)
        expect(c, terminator,
               terminator == ';' ? "expected ';' after statement" : "expected ')' after for step");
    return !c->failed;
}
static void link_patch(Front *c, int *head, int at) {
    if (at >= 0) {
        c->p->code[at].b = *head;
        *head = at;
    }
}
static void flush(Front *c, int head, int target) {
    while (head >= 0) {
        int next = c->p->code[head].b;
        c->p->code[head].a = target;
        c->p->code[head].b = 0;
        head = next;
    }
}
static Flow *push_flow(Front *c, int loop) {
    Flow *f;
    if (c->nflows >= MAX_NESTING) {
        fail(c, "control flow nesting limit");
        return NULL;
    }
    f = &c->flows[c->nflows++];
    f->breaks = f->continues = -1;
    f->is_loop = loop;
    f->continue_target = -1;
    return f;
}
static void pop_flow(Front *c, int end, int cont) {
    Flow f = c->flows[--c->nflows];
    flush(c, f.breaks, end);
    flush(c, f.continues, cont);
}
static int block(Front *c) {
    if (!expect(c, '{', "expected '{'"))
        return 0;
    while (!c->failed && kind(c) && kind(c) != '}')
        statement(c);
    return expect(c, '}', "expected '}' after block");
}
static int switch_statement(Front *c) {
    int slot, dispatch, skip, default_target = -1;
    struct Case {
        Token value;
        int target;
    } *cases = NULL;
    int n = 0, cap = 0, i;
    expect(c, '(', "expected '(' after switch");
    {
        Expr e = expression(c, 1);
        force(c, &e);
    }
    expect(c, ')', "expected ')' after switch value");
    slot = temporary(c);
    emit(c, GSC_SET_LOCAL, slot, 0, -1, 0);
    dispatch = emit(c, GSC_JUMP, 0, 0, -1, 0);
    if (!push_flow(c, 0))
        return 0;
    expect(c, '{', "expected '{' after switch");
    while (!c->failed && kind(c) && kind(c) != '}') {
        if (take_word(c, "case")) {
            Token value = *token(c);
            int negative = accept(c, '-');
            if (negative)
                value = *token(c);
            if (value.kind != T_INT && value.kind != T_STRING) {
                fail(c, "case label must be an integer or string literal");
                break;
            }
            c->pos++;
            if (negative) {
                uint32_t v = 0u - (uint32_t)value.integer;
                memcpy(&value.integer, &v, sizeof(v));
                if (value.kind != T_INT) {
                    fail(c, "cannot negate a string case");
                    break;
                }
            }
            for (i = 0; i < n; i++)
                if (cases[i].value.kind == value.kind &&
                    ((value.kind == T_INT && cases[i].value.integer == value.integer) ||
                     (value.kind == T_STRING && cases[i].value.id == value.id)))
                    fail(c, "duplicate case label");
            if (n >= cap) {
                int nc = cap ? cap * 2 : 16;
                struct Case *p = (struct Case *)realloc(cases, (size_t)nc * sizeof(*p));
                if (!p) {
                    fail(c, "out of memory in switch");
                    break;
                }
                cases = p;
                cap = nc;
            }
            cases[n].value = value;
            cases[n++].target = c->p->ncode;
            expect(c, ':', "expected ':' after case");
        } else if (take_word(c, "default")) {
            if (default_target >= 0)
                fail(c, "duplicate default label");
            default_target = c->p->ncode;
            expect(c, ':', "expected ':' after default");
        } else
            statement(c);
    }
    expect(c, '}', "expected '}' after switch");
    skip = emit(c, GSC_JUMP, 0, 0, -1, 0);
    patch(c, dispatch, c->p->ncode);
    for (i = 0; i < n; i++) {
        int next;
        emit(c, GSC_GET_LOCAL, slot, 0, -1, 0);
        if (cases[i].value.kind == T_INT)
            emit(c, GSC_PUSH_INT, cases[i].value.integer, 0, -1, 0);
        else
            emit(c, GSC_PUSH_STR, 0, 0, cases[i].value.id, 0);
        op(c, GSC_EQ);
        next = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0);
        emit(c, GSC_JUMP, cases[i].target, 0, -1, 0);
        patch(c, next, c->p->ncode);
    }
    if (default_target >= 0)
        emit(c, GSC_JUMP, default_target, 0, -1, 0);
    patch(c, skip, c->p->ncode);
    pop_flow(c, c->p->ncode, 0);
    free(cases);
    return !c->failed;
}
static int statement(Front *c) {
    int result = 1;
    if (c->failed)
        return 0;
    if (++c->depth > MAX_NESTING) {
        c->depth--;
        return fail(c, "statement nesting limit");
    }
    if (kind(c) == '{')
        result = block(c);
    else if (accept(c, ';')) {
    } else if (accept(c, T_DEVBEGIN)) {
        int skip = -1;
        if (!c->developer)
            skip = emit(c, GSC_JUMP, 0, 0, -1, 0);
        while (!c->failed && kind(c) && kind(c) != T_DEVEND)
            statement(c);
        expect(c, T_DEVEND, "expected '#/' after developer block");
        patch(c, skip, c->p->ncode);
    } else if (take_word(c, "if")) {
        int branch, end;
        Expr e;
        expect(c, '(', "expected '(' after if");
        e = expression(c, 1);
        force(c, &e);
        expect(c, ')', "expected ')' after if condition");
        branch = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0);
        statement(c);
        if (take_word(c, "else")) {
            end = emit(c, GSC_JUMP, 0, 0, -1, 0);
            patch(c, branch, c->p->ncode);
            statement(c);
            patch(c, end, c->p->ncode);
        } else
            patch(c, branch, c->p->ncode);
    } else if (take_word(c, "while")) {
        int start = c->p->ncode, end;
        Expr e;
        expect(c, '(', "expected '(' after while");
        e = expression(c, 1);
        force(c, &e);
        expect(c, ')', "expected ')' after while condition");
        end = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0);
        if (push_flow(c, 1)) {
            statement(c);
            emit(c, GSC_JUMP_BACK, start, 0, -1, 0);
            patch(c, end, c->p->ncode);
            pop_flow(c, c->p->ncode, start);
        }
    } else if (take_word(c, "do")) {
        int start = c->p->ncode, cond, end;
        Expr e;
        if (push_flow(c, 1)) {
            statement(c);
            cond = c->p->ncode;
            if (!take_word(c, "while"))
                fail(c, "expected while after do body");
            expect(c, '(', "expected '(' after do-while");
            e = expression(c, 1);
            force(c, &e);
            expect(c, ')', "expected ')' after do-while condition");
            expect(c, ';', "expected ';' after do-while");
            end = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0);
            emit(c, GSC_JUMP_BACK, start, 0, -1, 0);
            patch(c, end, c->p->ncode);
            pop_flow(c, c->p->ncode, cond);
        }
    } else if (take_word(c, "for")) {
        int cond, end, body, step;
        expect(c, '(', "expected '(' after for");
        if (!accept(c, ';'))
            assignment(c, ';');
        cond = c->p->ncode;
        if (kind(c) == ';')
            emit(c, GSC_PUSH_INT, 1, 0, -1, 0);
        else {
            Expr e = expression(c, 1);
            force(c, &e);
        }
        expect(c, ';', "expected ';' after for condition");
        end = emit(c, GSC_JUMP_IF_FALSE, 0, 0, -1, 0);
        body = emit(c, GSC_JUMP, 0, 0, -1, 0);
        step = c->p->ncode;
        if (!accept(c, ')'))
            assignment(c, ')');
        emit(c, GSC_JUMP_BACK, cond, 0, -1, 0);
        patch(c, body, c->p->ncode);
        if (push_flow(c, 1)) {
            statement(c);
            emit(c, GSC_JUMP_BACK, step, 0, -1, 0);
            patch(c, end, c->p->ncode);
            pop_flow(c, c->p->ncode, step);
        }
    } else if (take_word(c, "switch"))
        result = switch_statement(c);
    else if (word(c, "break") || word(c, "continue")) {
        int is_continue = take_word(c, "continue"), i = c->nflows - 1, jump;
        if (!is_continue)
            c->pos++;
        if (is_continue)
            while (i >= 0 && !c->flows[i].is_loop)
                i--;
        if (i < 0)
            fail(c, is_continue ? "continue outside loop" : "break outside loop/switch");
        else {
            jump = emit(c, GSC_JUMP, 0, 0, -1, 0);
            link_patch(c, is_continue ? &c->flows[i].continues : &c->flows[i].breaks, jump);
        }
        expect(c, ';', "expected ';' after control statement");
    } else if (take_word(c, "return")) {
        if (kind(c) == ';')
            op(c, GSC_PUSH_UNDEF);
        else {
            Expr e = expression(c, 1);
            force(c, &e);
        }
        expect(c, ';', "expected ';' after return");
        op(c, GSC_RETURN);
    } else if (take_word(c, "wait")) {
        Expr e = expression(c, 1);
        force(c, &e);
        expect(c, ';', "expected ';' after wait");
        op(c, GSC_WAIT);
    } else if (take_word(c, "waittillframeend")) {
        expect(c, ';', "expected ';' after waittillframeend");
        op(c, GSC_FRAME_END);
    } else
        result = assignment(c, ';');
    c->depth--;
    return result && !c->failed;
}
static int directive(Front *c) {
    int include;
    expect(c, '#', "expected directive");
    include = take_word(c, "include");
    if (include) {
        char path[256];
        size_t n = 0;
        if (kind(c) != T_ID)
            return fail(c, "expected include path");
        for (;;) {
            const char *s = text(c, token(c)->id);
            size_t z = strlen(s);
            if (n + z + 5 >= sizeof(path))
                return fail(c, "include path too long");
            memcpy(path + n, s, z);
            n += z;
            c->pos++;
            if (kind(c) != '\\' && kind(c) != '/')
                break;
            path[n++] = '/';
            c->pos++;
            if (kind(c) != T_ID)
                return fail(c, "expected include path component");
        }
        memcpy(path + n, ".gsc", 5);
        emit(c, GSC_IMPORT, intern(c, path, n + 4), 0, -1, 0);
        expect(c, ';', "expected ';' after include");
    } else if (take_word(c, "using_animtree")) {
        expect(c, '(', "expected '(' after using_animtree");
        if (kind(c) != T_STRING)
            return fail(c, "expected animation tree string");
        c->animtree = token(c)->id;
        c->pos++;
        expect(c, ')', "expected ')' after animation tree");
        expect(c, ';', "expected ';' after using_animtree");
    } else
        return fail(c, "unknown preprocessor directive");
    return !c->failed;
}
static int discover(Front *c) {
    int developer = 0;
    while (!c->failed && kind(c)) {
        Signature sig;
        int depth, i;
        memset(&sig, 0, sizeof(sig));
        if (accept(c, T_DEVBEGIN)) {
            if (developer)
                return fail(c, "nested developer declaration block");
            developer = 1;
            continue;
        }
        if (accept(c, T_DEVEND)) {
            if (!developer)
                return fail(c, "unexpected '#/'");
            developer = 0;
            continue;
        }
        if (kind(c) == '#') {
            directive(c);
            continue;
        }
        if (kind(c) != T_ID)
            return fail(c, "expected function definition or directive");
        sig.name = token(c)->id;
        if (c->reserved_names[sig.name])
            return fail(c, "reserved word cannot name a function");
        sig.developer = developer;
        sig.animtree = c->animtree;
        c->pos++;
        if (function(c, sig.name) >= 0)
            return fail(c, "duplicate function definition");
        expect(c, '(', "expected '(' after function name");
        sig.params = c->pos;
        if (!accept(c, ')')) {
            do {
                if (kind(c) != T_ID)
                    return fail(c, "expected parameter name");
                c->pos++;
            } while (accept(c, ','));
            expect(c, ')', "expected ')' after parameters");
        }
        expect(c, '{', "expected function body");
        sig.body = c->pos;
        depth = 1;
        while (!c->failed && kind(c) && depth) {
            if (kind(c) == '{')
                depth++;
            else if (kind(c) == '}')
                depth--;
            if (depth)
                c->pos++;
        }
        if (depth)
            return fail(c, "unterminated function body");
        sig.end = c->pos;
        c->pos++;
        if (c->p->nfunc >= MAX_FUNCTIONS)
            return fail(c, "function limit exceeded");
        if (c->p->nfunc >= c->p->fcap) {
            int cap = c->p->fcap ? c->p->fcap * 2 : 32;
            GscFunc *funcs = (GscFunc *)realloc(c->p->funcs, (size_t)cap * sizeof(GscFunc));
            Signature *signatures;
            if (!funcs)
                return fail(c, "out of memory in functions");
            c->p->funcs = funcs;
            signatures = (Signature *)realloc(c->signatures, (size_t)cap * sizeof(Signature));
            if (!signatures)
                return fail(c, "out of memory in signatures");
            c->signatures = signatures;
            c->p->fcap = cap;
        }
        i = c->p->nfunc++;
        c->functions_by_name[sig.name] = i;
        c->signatures[i] = sig;
        memset(&c->p->funcs[i], 0, sizeof(GscFunc));
        snprintf(c->p->funcs[i].name, sizeof(c->p->funcs[i].name), "%s", text(c, sig.name));
    }
    if (developer)
        return fail(c, "unterminated developer declaration block");
    return !c->failed;
}
int gsc_compile_runtime_options(const char *source, size_t size, GscProg *prog,
                                const GscCompileOptions *options,
                                GscCompileDiagnostic *diagnostic) {
    Front c;
    int fi, ok = 0;
    if (diagnostic)
        memset(diagnostic, 0, sizeof(*diagnostic));
    if (!prog)
        return 0;
    memset(prog, 0, sizeof(*prog));
    memset(&c, 0, sizeof(c));
    c.p = prog;
    c.error = diagnostic;
    c.source = source;
    c.size = size;
    c.animtree = -1;
    c.developer = options && options->developer;
    prog->fuel = 100000;
    if (!source || size > MAX_SOURCE) {
        fail_at(&c, NULL, "invalid source or source size limit");
        goto done;
    }
    if (!lex(&c))
        goto done;
    c.function_names = prog->nstr;
    if (c.function_names < 0 || c.function_names > MAX_TOKENS) {
        fail_at(&c, NULL, "symbol count limit");
        goto done;
    }
    c.reserved_names =
        (unsigned char *)calloc((size_t)(c.function_names ? c.function_names : 1), 1);
    if (!c.reserved_names) {
        fail_at(&c, NULL, "out of memory in keyword symbols");
        goto done;
    }
    {
        static const char *const keywords[] = {
            "self",     "level",         "game",   "anim",   "true",
            "false",    "undefined",     "if",     "else",   "while",
            "do",       "for",           "switch", "case",   "default",
            "break",    "continue",      "return", "thread", "wait",
            "waittill", "waittillmatch", "notify", "endon",  "waittillframeend"};
        size_t k;
        for (k = 0; c.hcap && k < sizeof(keywords) / sizeof(keywords[0]); k++) {
            unsigned slot = hash_bytes(keywords[k], strlen(keywords[k])) & (unsigned)(c.hcap - 1);
            while (c.hashes[slot]) {
                int id = c.hashes[slot] - 1;
                if (!strcmp(text(&c, id), keywords[k])) {
                    c.reserved_names[id] = 1;
                    break;
                }
                slot = (slot + 1) & (unsigned)(c.hcap - 1);
            }
        }
    }
    c.functions_by_name = (int *)malloc((size_t)(unsigned)(c.function_names + 1) * sizeof(int));
    if (!c.functions_by_name) {
        fail_at(&c, NULL, "out of memory in function symbols");
        goto done;
    }
    for (fi = 0; fi < c.function_names; fi++)
        c.functions_by_name[fi] = -1;
    if (!discover(&c))
        goto done;
    for (fi = 0; fi < prog->nfunc && !c.failed; fi++) {
        Signature *sig = &c.signatures[fi];
        GscFunc *fn = &prog->funcs[fi];
        c.fn = fi;
        c.nlocals = c.temp = c.nflows = c.depth = 0;
        c.animtree = sig->animtree;
        c.pos = sig->params;
        memset(c.localhash, 0, sizeof(c.localhash));
        while (kind(&c) != ')' && !c.failed) {
            int id = token(&c)->id, previous = c.nlocals;
            if (kind(&c) != T_ID) {
                fail(&c, "invalid parameter");
                break;
            }
            local(&c, id);
            if (previous == c.nlocals) {
                /* Retail IW3 contains empty helpers such as f(name,val,val,val).
                 * Their unused arguments still occupy distinct positional slots.
                 * Keep ambiguous references in nonempty bodies an error. */
                if (sig->body == sig->end)
                    temporary(&c);
                else
                    fail(&c, "duplicate parameter name");
            }
            c.pos++;
            if (!accept(&c, ','))
                break;
        }
        fn->nparams = c.nlocals;
        if (fn->nparams > MAX_ARGS) {
            fail(&c, "parameter limit exceeded");
            break;
        }
        fn->entry = prog->ncode;
        if (sig->developer && !c.developer) {
            op(&c, GSC_PUSH_UNDEF);
            op(&c, GSC_RETURN);
        }
        c.pos = sig->body;
        while (c.pos < sig->end && !c.failed)
            statement(&c);
        if (c.pos != sig->end && !c.failed)
            fail(&c, "unexpected function boundary");
        op(&c, GSC_PUSH_UNDEF);
        op(&c, GSC_RETURN);
        fn->nlocals = c.nlocals;
    }
    ok = !c.failed;
done:
    free(c.tokens);
    free(c.hashes);
    free(c.signatures);
    free(c.functions_by_name);
    free(c.reserved_names);
    free(c.references);
    if (!ok)
        gsc_prog_free(prog);
    return ok;
}
