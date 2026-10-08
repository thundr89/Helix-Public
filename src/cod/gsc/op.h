/*
 * GSC stack-machine opcodes, instruction record, and program layout.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef GSC_OP_H
#define GSC_OP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GscOp {
    GSC_RETURN = 1,
    GSC_PUSH_UNDEF,
    GSC_PUSH_INT,
    GSC_PUSH_FLOAT,
    GSC_PUSH_STR,
    GSC_GET_LOCAL,
    GSC_SET_LOCAL,
    GSC_ADD, GSC_SUB, GSC_MUL, GSC_DIV,
    GSC_LT, GSC_GT, GSC_EQ,
    GSC_JUMP, GSC_JUMP_IF_FALSE,
    GSC_CALL,    /* a = function index, b = argc */
    GSC_BUILTIN, /* a = builtin id, b = argc */
    GSC_NEW_ARRAY,
    GSC_GET_INDEX, /* pops index, then base */
    GSC_SET_INDEX, /* pops value, index, base */
    GSC_GET_FIELD, /* a = string index of the field name */
    GSC_SET_FIELD,
    GSC_JUMP_BACK, /* a = absolute instruction index; counts as a back edge for fuel */
    GSC_FAR,       /* a = path string index, b = function name string index, str = argc */
    GSC_THREAD,    /* local: same as GSC_CALL (str = -1). far: str >= 0, a and b are path and name string indexes, str is argc */
    GSC_STOP       /* end this branch; the thread's caller continues */
} GscOp;

enum { GSC_BU_GIVE = 1, GSC_BU_TAKE, GSC_BU_AMBIENT, GSC_BU_GAMETYPE, GSC_BU_ALLIES, GSC_BU_AXIS, GSC_BU_STRUCT };

typedef struct GscIns {
    unsigned char op;
    int a, b;
    int str; /* string-table index, argc for GSC_FAR and far GSC_THREAD, or -1 */
    double f;
} GscIns;

typedef struct GscFunc {
    char name[64];
    int entry;
    int nparams;
    int nlocals;
} GscFunc;

typedef struct GscProg {
    GscIns* code;
    int ncode, cap;
    GscFunc* funcs;
    int nfunc, fcap;
    char* strs;
    int* stroff;
    int nstr, scap;
    int fuel;
} GscProg;

#ifdef __cplusplus
}
#endif

#endif /* GSC_OP_H */
