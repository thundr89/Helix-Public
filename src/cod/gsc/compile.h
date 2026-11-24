/*
 * GSC source-to-bytecode compiler for the stack machine in vm.c.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef GSC_COMPILE_H
#define GSC_COMPILE_H

#include <stddef.h>

#include "gsc/op.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 and fills prog. Caller frees with gsc_prog_free. */
int gsc_compile(const char* source, size_t size, GscProg* prog);
int gsc_compile_runtime(const char* source, size_t size, GscProg* prog);
typedef struct GscCompileDiagnostic {
    size_t offset;
    unsigned line, column;
    char message[192];
} GscCompileDiagnostic;
typedef struct GscCompileOptions { int developer; } GscCompileOptions;
int gsc_compile_runtime_options(const char* source, size_t size, GscProg* prog,
                                const GscCompileOptions* options, GscCompileDiagnostic* diagnostic);
void gsc_prog_free(GscProg* prog);

#ifdef __cplusplus
}
#endif

#endif /* GSC_COMPILE_H */
